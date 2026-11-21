// Copyright (C) 2007 Id Software, Inc.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "idlib/precompiled.h"
#include "posix_public.h"
#include "../LocalSubnets.h"
#include "../NetAddress.h"

idCVar net_ip( "net_ip", "localhost", CVAR_SYSTEM, "local IP address" );
idCVar net_port( "net_port", "0", CVAR_SYSTEM | CVAR_INTEGER, "local IP port number" );
idCVar net_ipv6( "net_ipv6", "1", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "also use IPv6: each network port opens an IPv6 socket next to its IPv4 one" );
idCVar net_forceDrop( "net_forceDrop", "0", CVAR_SYSTEM | CVAR_INTEGER, "percentage packet loss" );	// as win_net.cpp: outgoing packets
idCVar net_forceLatency( "net_forceLatency", "0", CVAR_SYSTEM | CVAR_INTEGER, "milliseconds latency added to received packets" );
idCVar net_forceJitter( "net_forceJitter", "0", CVAR_SYSTEM | CVAR_INTEGER, "up to this many more milliseconds of random latency per received packet (order kept)" );
idCVar net_forceReorder( "net_forceReorder", "0", CVAR_SYSTEM | CVAR_INTEGER, "percentage of received packets held back so that later packets overtake them (network testing)" );
idCVar net_forceReorderDelay( "net_forceReorderDelay", "30", CVAR_SYSTEM | CVAR_INTEGER, "up to this many milliseconds a net_forceReorder packet is held behind the packets after it" );

// Received packets held back by net_forceLatency / net_forceJitter (network testing).
// Keyed by socket, so idPort (shared with the game module) is unchanged.
struct delayedPacket_t {
	int				socket;
	int				releaseTime;
	int				sequence;		// arrival order on this socket, to count overtakes
	netadr_t		from;
	idList< byte >	data;
};
static idList< delayedPacket_t* > delayedPackets;

// last release time per socket: jitter varies the delay but keeps the arrival
// order, like queueing on a real path (independent per-packet delays reordered
// the fragments of every large message, which the channel then dropped)
struct socketRelease_t {
	int		socket;
	int		releaseTime;
	int		arrived;			// packets queued on this socket
	int		lastReleasedSequence;
	int		released;
	int		reordered;		// released after a packet that arrived later
};
static idList< socketRelease_t > socketReleases;

static int Net_ReleaseTime( int netSocket, int wanted ) {
	for ( int i = 0; i < socketReleases.Num(); i++ ) {
		if ( socketReleases[ i ].socket == netSocket ) {
			socketReleases[ i ].releaseTime = Max( wanted, socketReleases[ i ].releaseTime );
			return socketReleases[ i ].releaseTime;
		}
	}
	socketRelease_t release = { netSocket, wanted, 0, 0, 0, 0 };
	socketReleases.Append( release );
	return wanted;
}

static socketRelease_t* Net_SocketRelease( int netSocket ) {
	for ( int i = 0; i < socketReleases.Num(); i++ ) {
		if ( socketReleases[ i ].socket == netSocket ) {
			return &socketReleases[ i ];
		}
	}
	Net_ReleaseTime( netSocket, 0 );
	return &socketReleases[ socketReleases.Num() - 1 ];
}

// a closed socket's descriptor is reused by the next one opened
static void Net_PurgeSocket( int netSocket ) {
	for ( int i = delayedPackets.Num() - 1; i >= 0; i-- ) {
		if ( delayedPackets[ i ]->socket == netSocket ) {
			delete delayedPackets[ i ];
			delayedPackets.RemoveIndex( i );
		}
	}
	for ( int i = socketReleases.Num() - 1; i >= 0; i-- ) {
		if ( socketReleases[ i ].socket == netSocket ) {
			if ( net_forceReorder.GetInteger() > 0 ) {
				common->Printf( "NET_REORDER: %d of %d received packets released after a later one\n",
					socketReleases[ i ].reordered, socketReleases[ i ].released );
			}
			socketReleases.RemoveIndex( i );
		}
	}
}

// an NA_IP6 address, with the key in ip (NetAddress.h)
static void Net_SetIPv6( netadr_t& a, const uint8_t ip6[ 16 ], unsigned int scope, unsigned short port ) {
	memset( &a, 0, sizeof( a ) );
	a.type = NA_IP6;
	memcpy( a.ip6, ip6, 16 );
	a.scope = NA_IsLinkLocalIPv6( ip6 ) ? scope : 0;
	a.port = port;
	NA_Key32( ip6, a.ip );
}

static bool Net_SockaddrToNetadr( const struct sockaddr_storage& address, netadr_t& out ) {
	if ( address.ss_family == AF_INET ) {
		const struct sockaddr_in& in = reinterpret_cast< const struct sockaddr_in& >( address );
		memset( &out, 0, sizeof( out ) );
		out.type = NA_IP;
		out.port = ntohs( in.sin_port );
		memcpy( out.ip, &in.sin_addr.s_addr, 4 );
		return true;
	}
	if ( address.ss_family == AF_INET6 ) {
		const struct sockaddr_in6& in6 = reinterpret_cast< const struct sockaddr_in6& >( address );
		const uint8_t* bytes = reinterpret_cast< const uint8_t* >( &in6.sin6_addr );
		if ( NA_IsV4Mapped( bytes ) ) {
			memset( &out, 0, sizeof( out ) );
			out.type = NA_IP;
			out.port = ntohs( in6.sin6_port );
			memcpy( out.ip, bytes + 12, 4 );
			return true;
		}
		Net_SetIPv6( out, bytes, in6.sin6_scope_id, ntohs( in6.sin6_port ) );
		return true;
	}
	return false;
}

static bool Net_ReadSocket( int netSocket, netadr_t &from, void *data, int &size, int maxSize ) {
	if ( netSocket == 0 ) {
		return false;
	}
	struct sockaddr_storage from_addr;
	socklen_t fromlen = sizeof( from_addr );
	int ret = recvfrom( netSocket, static_cast< char* >( data ), maxSize, 0, reinterpret_cast< struct sockaddr* >( &from_addr ), &fromlen );
	if ( ret <= 0 ) {
		return false;
	}
	if ( !Net_SockaddrToNetadr( from_addr, from ) ) {
		return false;
	}
	size = ret;
	return true;
}

// a non-blocking UDP socket bound to the port (0: any), or -1
static int Net_OpenSocket( int family, int port ) {
	int s = socket( family, SOCK_DGRAM, IPPROTO_UDP );
	if ( s < 0 ) {
		return -1;
	}
	int flags = fcntl( s, F_GETFL, 0 );
	if ( flags != -1 ) {
		fcntl( s, F_SETFL, flags | O_NONBLOCK );
	}
	int opt = 1;
	setsockopt( s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof( opt ) );
	if ( family == AF_INET ) {
		setsockopt( s, SOL_SOCKET, SO_BROADCAST, &opt, sizeof( opt ) );
		struct sockaddr_in address;
		memset( &address, 0, sizeof( address ) );
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons( static_cast< unsigned short >( port ) );
		if ( bind( s, reinterpret_cast< struct sockaddr* >( &address ), sizeof( address ) ) < 0 ) {
			close( s );
			return -1;
		}
		return s;
	}
	// IPv6 only: IPv4 stays on its own socket, which can broadcast
	setsockopt( s, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof( opt ) );
	struct sockaddr_in6 address;
	memset( &address, 0, sizeof( address ) );
	address.sin6_family = AF_INET6;
	address.sin6_addr = in6addr_any;
	address.sin6_port = htons( static_cast< unsigned short >( port ) );
	if ( bind( s, reinterpret_cast< struct sockaddr* >( &address ), sizeof( address ) ) < 0 ) {
		close( s );
		return -1;
	}
	return s;
}

typedef struct {
	uint32_t ip;
	uint32_t mask;
} net_interface;

#define MAX_INTERFACES 16
static int num_interfaces = 0;
static net_interface netint[ MAX_INTERFACES ];

idPort::idPort() {
	netSocket = 0;
	netSocket6 = 0;
	memset( &bound_to, 0, sizeof( bound_to ) );
	packetsRead = 0;
	bytesRead = 0;
	packetsWritten = 0;
	bytesWritten = 0;
	silent = false;
}

idPort::~idPort() {
	Close();
}

void idPort::Close() {
	if ( netSocket != 0 ) {
		Net_PurgeSocket( netSocket );
		close( netSocket );
		netSocket = 0;
	}
	if ( netSocket6 != 0 ) {
		close( netSocket6 );
		netSocket6 = 0;
	}
	memset( &bound_to, 0, sizeof( bound_to ) );
}

bool idPort::InitForPort( int portNumber ) {
	Close();

	const int wanted = ( portNumber == PORT_ANY || portNumber < 0 ) ? 0 : portNumber;
	int s = Net_OpenSocket( AF_INET, wanted );
	if ( s < 0 ) {
		common->Warning( "idPort::InitForPort: couldn't open port %d: %s", portNumber, strerror( errno ) );
		return false;
	}

	struct sockaddr_in address;
	memset( &address, 0, sizeof( address ) );
	socklen_t len = sizeof( address );
	bound_to.type = NA_IP;
	if ( getsockname( s, reinterpret_cast< struct sockaddr* >( &address ), &len ) == 0 ) {
		bound_to.port = ntohs( address.sin_port );
	} else {
		bound_to.port = static_cast< unsigned short >( wanted );
	}
	netSocket = s;

	// IPv6 on the same port; a client's port can be any free one
	if ( net_ipv6.GetBool() ) {
		int s6 = Net_OpenSocket( AF_INET6, bound_to.port );
		if ( s6 < 0 && wanted == 0 ) {
			s6 = Net_OpenSocket( AF_INET6, 0 );
		}
		if ( s6 >= 0 ) {
			netSocket6 = s6;
		} else {
			common->DPrintf( "idPort::InitForPort: no IPv6 on port %d: %s\n", bound_to.port, strerror( errno ) );
		}
	}
	return true;
}

bool idPort::GetPacket( netadr_t &from, void *data, int &size, int maxSize ) {
	if ( netSocket == 0 ) {
		return false;
	}

	const int latency = Max( 0, net_forceLatency.GetInteger() );
	const int jitter = Max( 0, net_forceJitter.GetInteger() );
	const int reorderPercent = idMath::ClampInt( 0, 100, net_forceReorder.GetInteger() );
	const int reorderDelay = idMath::ClampInt( 1, 10000, net_forceReorderDelay.GetInteger() );	// within MSVC's RAND_MAX (32767)
	if ( latency == 0 && jitter == 0 && reorderPercent == 0 && delayedPackets.Num() == 0 ) {
		if ( !Net_ReadSocket( netSocket, from, data, size, maxSize ) && !Net_ReadSocket( netSocket6, from, data, size, maxSize ) ) {
			return false;
		}
		packetsRead++;
		bytesRead += size;
		return true;
	}

	// queue everything that has arrived, each with its own release time
	const int now = Sys_Milliseconds();
	netadr_t packetFrom;
	int packetSize = 0;
	while ( Net_ReadSocket( netSocket, packetFrom, data, packetSize, maxSize ) || Net_ReadSocket( netSocket6, packetFrom, data, packetSize, maxSize ) ) {
		delayedPacket_t* packet = new delayedPacket_t;
		packet->socket = netSocket;
		const int wanted = now + latency + ( jitter > 0 ? rand() % ( jitter + 1 ) : 0 );
		if ( reorderPercent > 0 && rand() % 100 < reorderPercent ) {
			// held behind the packets that arrive after it: later than the
			// in-order release time, which it doesn't advance
			const int inOrder = Max( wanted, Net_SocketRelease( netSocket )->releaseTime );
			packet->releaseTime = inOrder + 1 + rand() % reorderDelay;
		} else {
			packet->releaseTime = Net_ReleaseTime( netSocket, wanted );
		}
		packet->sequence = ++Net_SocketRelease( netSocket )->arrived;
		packet->from = packetFrom;
		packet->data.SetNum( packetSize );
		memcpy( &packet->data[ 0 ], data, packetSize );
		delayedPackets.Append( packet );
	}

	// hand out the due packet of this socket that is released first
	int best = -1;
	for ( int i = 0; i < delayedPackets.Num(); i++ ) {
		const delayedPacket_t* packet = delayedPackets[ i ];
		if ( packet->socket == netSocket && packet->releaseTime <= now && ( best < 0 || packet->releaseTime < delayedPackets[ best ]->releaseTime ) ) {
			best = i;
		}
	}
	if ( best < 0 ) {
		return false;
	}
	delayedPacket_t* packet = delayedPackets[ best ];
	delayedPackets.RemoveIndex( best );
	socketRelease_t* release = Net_SocketRelease( netSocket );
	release->released++;
	if ( packet->sequence < release->lastReleasedSequence ) {
		release->reordered++;
	} else {
		release->lastReleasedSequence = packet->sequence;
	}
	from = packet->from;
	size = Min( packet->data.Num(), maxSize );
	memcpy( data, &packet->data[ 0 ], size );
	delete packet;
	packetsRead++;
	bytesRead += size;
	return true;
}

bool idPort::GetPacketBlocking( netadr_t &from, void *data, int &size, int maxSize, int timeout ) {
	if ( netSocket == 0 ) {
		return false;
	}

	// a packet held back by net_forceLatency / net_forceJitter can be due already,
	// and the socket does not wake select when one becomes due
	if ( GetPacket( from, data, size, maxSize ) ) {
		return true;
	}
	const int now = Sys_Milliseconds();
	for ( int i = 0; i < delayedPackets.Num(); i++ ) {
		if ( delayedPackets[ i ]->socket == netSocket ) {
			timeout = Min( timeout, Max( 0, delayedPackets[ i ]->releaseTime - now ) );
		}
	}

	fd_set readfds;
	FD_ZERO( &readfds );
	FD_SET( netSocket, &readfds );
	if ( netSocket6 != 0 ) {
		FD_SET( netSocket6, &readfds );
	}

	struct timeval tv;
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = ( timeout % 1000 ) * 1000;

	if ( select( Max( netSocket, netSocket6 ) + 1, &readfds, NULL, NULL, &tv ) < 0 ) {
		return false;
	}

	return GetPacket( from, data, size, maxSize );
}

void idPort::SendPacket( const netadr_t to, const void *data, int size ) {
	if ( netSocket == 0 || size <= 0 || data == NULL ) {
		return;
	}

	struct sockaddr_storage storage;
	memset( &storage, 0, sizeof( storage ) );
	socklen_t addrSize = 0;
	int sendSocket = netSocket;
	if ( to.type == NA_IP6 ) {
		if ( netSocket6 == 0 ) {
			return;
		}
		struct sockaddr_in6& addr6 = reinterpret_cast< struct sockaddr_in6& >( storage );
		addr6.sin6_family = AF_INET6;
		addr6.sin6_port = htons( to.port );
		memcpy( &addr6.sin6_addr, to.ip6, 16 );
		addr6.sin6_scope_id = to.scope;
		addrSize = sizeof( addr6 );
		sendSocket = netSocket6;
	} else {
		struct sockaddr_in& addr = reinterpret_cast< struct sockaddr_in& >( storage );
		addr.sin_family = AF_INET;
		addr.sin_port = htons( to.port );
		if ( to.type == NA_BROADCAST ) {
			addr.sin_addr.s_addr = INADDR_BROADCAST;
		} else if ( to.type == NA_IP ) {
			memcpy( &addr.sin_addr.s_addr, to.ip, 4 );
		} else if ( to.type == NA_LOOPBACK ) {
			addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
		} else {
			return;
		}
		addrSize = sizeof( addr );
	}

	const int dropPercent = idMath::ClampInt( 0, 100, net_forceDrop.GetInteger() );
	if ( dropPercent > 0 && rand() < dropPercent * ( RAND_MAX / 100 ) ) {
		packetsWritten++;	// counted as sent, as a lost packet would be
		bytesWritten += size;
		return;
	}

	int ret = sendto( sendSocket, static_cast< const char* >( data ), size, 0, reinterpret_cast< struct sockaddr* >( &storage ), addrSize );
	if ( ret > 0 ) {
		packetsWritten++;
		bytesWritten += ret;
	}
}

// "fe80::1%eth0" or "%3": the address and its zone's interface index
static bool Net_ParseIPv6WithZone( const char* host, uint8_t ip6[ 16 ], unsigned int& scope ) {
	char address[ 64 ];
	idStr::Copynz( address, host, sizeof( address ) );
	scope = 0;
	char* zone = strchr( address, '%' );
	if ( zone != NULL ) {
		*zone++ = '\0';
		char* end = NULL;
		const unsigned long number = strtoul( zone, &end, 10 );
		scope = ( end != zone && *end == '\0' ) ? static_cast< unsigned int >( number ) : if_nametoindex( zone );
		if ( scope == 0 ) {
			return false;
		}
	}
	return NA_ParseIPv6( address, ip6 );
}

bool Sys_StringToNetAdr( const char *s, netadr_t *a, bool doDNSResolve ) {
	if ( s == NULL || a == NULL || s[0] == '\0' ) {
		return false;
	}

	memset( a, 0, sizeof( *a ) );

	if ( idStr::Icmp( s, "localhost" ) == 0 || idStr::Icmp( s, "127.0.0.1" ) == 0 ) {
		a->type = NA_LOOPBACK;
		return true;
	}

	char host[ 256 ];
	int port = 0;
	if ( !NA_SplitHostPort( s, host, sizeof( host ), &port ) ) {
		return false;
	}

	// "localhost" with a port too, without a DNS lookup (callers may pass
	// doDNSResolve false)
	if ( idStr::Icmp( host, "localhost" ) == 0 ) {
		a->type = NA_LOOPBACK;
		a->port = static_cast< unsigned short >( port );
		return true;
	}

	struct in_addr inaddr;
	if ( inet_aton( host, &inaddr ) != 0 ) {
		a->type = NA_IP;
		memcpy( a->ip, &inaddr.s_addr, 4 );
		a->port = static_cast< unsigned short >( port );
		return true;
	}

	uint8_t ip6[ 16 ];
	unsigned int scope = 0;
	if ( strchr( host, ':' ) != NULL ) {
		if ( !Net_ParseIPv6WithZone( host, ip6, scope ) ) {
			return false;
		}
		if ( NA_IsV4Mapped( ip6 ) ) {
			a->type = NA_IP;
			memcpy( a->ip, ip6 + 12, 4 );
			a->port = static_cast< unsigned short >( port );
			return true;
		}
		Net_SetIPv6( *a, ip6, scope, static_cast< unsigned short >( port ) );
		return true;
	}

	if ( doDNSResolve ) {
		// IPv4 first, as before IPv6 (any server reachable before still is the same way)
		struct addrinfo hints;
		memset( &hints, 0, sizeof( hints ) );
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;
		struct addrinfo* results = NULL;
		if ( getaddrinfo( host, NULL, &hints, &results ) == 0 ) {
			bool found = false;
			for ( int pass = 0; pass < 2 && !found; pass++ ) {
				for ( struct addrinfo* r = results; r != NULL && !found; r = r->ai_next ) {
					if ( ( pass == 0 && r->ai_family != AF_INET ) || ( pass == 1 && r->ai_family != AF_INET6 ) ) {
						continue;
					}
					struct sockaddr_storage storage;
					memset( &storage, 0, sizeof( storage ) );
					memcpy( &storage, r->ai_addr, Min( static_cast< size_t >( r->ai_addrlen ), sizeof( storage ) ) );
					found = Net_SockaddrToNetadr( storage, *a );
				}
			}
			freeaddrinfo( results );
			if ( found ) {
				a->port = static_cast< unsigned short >( port );
				return true;
			}
		}
	}

	return false;
}

const char *Sys_NetAdrToString( const netadr_t& a ) {
	static char s[ 4 ][ 80 ];
	static int index = 0;
	index = ( index + 1 ) & 3;

	if ( a.type == NA_LOOPBACK ) {
		idStr::snPrintf( s[ index ], sizeof( s[ index ] ), "localhost:%i", a.port );
	} else if ( a.type == NA_IP || a.type == NA_BROADCAST ) {
		idStr::snPrintf( s[ index ], sizeof( s[ index ] ), "%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port );
	} else if ( a.type == NA_IP6 ) {
		char text[ 48 ];
		NA_FormatIPv6( a.ip6, text, sizeof( text ) );
		if ( a.scope != 0 ) {
			idStr::snPrintf( s[ index ], sizeof( s[ index ] ), "[%s%%%u]:%i", text, a.scope, a.port );
		} else {
			idStr::snPrintf( s[ index ], sizeof( s[ index ] ), "[%s]:%i", text, a.port );
		}
	} else {
		idStr::snPrintf( s[ index ], sizeof( s[ index ] ), "bad_address" );
	}
	return s[ index ];
}

bool Sys_NetAdrToHostName( const netadr_t& a, char** s ) {
	return false;
}

bool Sys_IsLANAddress( const netadr_t& a ) {
	if ( a.type == NA_LOOPBACK ) {
		return true;
	}
	if ( a.type == NA_IP6 ) {
		return NA_IsLANIPv6( a.ip6 );
	}
	if ( a.type != NA_IP ) {
		return false;
	}
	if ( a.ip[0] == 10 ) return true;
	if ( a.ip[0] == 172 && ( a.ip[1] >= 16 && a.ip[1] <= 31 ) ) return true;
	if ( a.ip[0] == 192 && a.ip[1] == 168 ) return true;
	if ( a.ip[0] == 127 ) return true;
	return false;
}

// "localhost" and "127.0.0.1" parse to NA_LOOPBACK, but packets from the local
// host arrive as NA_IP 127.x (received addresses are always NA_IP): the same
// machine either way.  Without this, "connect localhost" never matched the
// server's replies.
static bool Net_IsLocalHost( const netadr_t& a ) {
	return a.type == NA_LOOPBACK || ( a.type == NA_IP && a.ip[0] == 127 ) || ( a.type == NA_IP6 && NA_IsLoopbackIPv6( a.ip6 ) );
}

bool Sys_CompareNetAdrBase( const netadr_t& a, const netadr_t& b ) {
	if ( a.type == NA_LOOPBACK || b.type == NA_LOOPBACK ) {
		return Net_IsLocalHost( a ) && Net_IsLocalHost( b );
	}
	if ( a.type != b.type ) {
		return false;
	}
	if ( a.type == NA_IP6 ) {
		return memcmp( a.ip6, b.ip6, 16 ) == 0 && a.scope == b.scope;
	}
	return ( memcmp( a.ip, b.ip, 4 ) == 0 );
}

int Sys_GetLocalIPCount( void ) {
	return num_interfaces > 0 ? num_interfaces : 1;
}

const char *Sys_GetLocalIP( int i ) {
	static char s[ 64 ];
	if ( i >= 0 && i < num_interfaces ) {
		const uint8_t *ip = reinterpret_cast< const uint8_t* >( &netint[ i ].ip );
		idStr::snPrintf( s, sizeof( s ), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3] );
		return s;
	}
	return "127.0.0.1";
}

int Sys_GetLocalSubnets( lanSubnet_t* out, int max ) {
	struct ifaddrs* list = NULL;
	if ( out == NULL || max <= 0 || getifaddrs( &list ) != 0 ) {
		return 0;
	}
	int count = 0;
	for ( struct ifaddrs* entry = list; entry != NULL && count < max; entry = entry->ifa_next ) {
		if ( entry->ifa_addr == NULL || entry->ifa_netmask == NULL || entry->ifa_addr->sa_family != AF_INET ) {
			continue;
		}
		if ( ( entry->ifa_flags & IFF_UP ) == 0 || ( entry->ifa_flags & IFF_LOOPBACK ) != 0 ) {
			continue;
		}
		out[ count ].address = ntohl( reinterpret_cast< const struct sockaddr_in* >( entry->ifa_addr )->sin_addr.s_addr );
		out[ count ].mask = ntohl( reinterpret_cast< const struct sockaddr_in* >( entry->ifa_netmask )->sin_addr.s_addr );
		count++;
	}
	freeifaddrs( list );
	return count;
}

int Sys_GetLocalIPv6( localAddress6_t* out, int max ) {
	struct ifaddrs* list = NULL;
	if ( out == NULL || max <= 0 || getifaddrs( &list ) != 0 ) {
		return 0;
	}
	int count = 0;
	for ( struct ifaddrs* entry = list; entry != NULL && count < max; entry = entry->ifa_next ) {
		if ( entry->ifa_addr == NULL || entry->ifa_addr->sa_family != AF_INET6 ) {
			continue;
		}
		if ( ( entry->ifa_flags & IFF_UP ) == 0 || ( entry->ifa_flags & IFF_LOOPBACK ) != 0 || ( entry->ifa_flags & IFF_MULTICAST ) == 0 ) {
			continue;
		}
		const struct sockaddr_in6* in6 = reinterpret_cast< const struct sockaddr_in6* >( entry->ifa_addr );
		memcpy( out[ count ].address, &in6->sin6_addr, 16 );
		out[ count ].scope = if_nametoindex( entry->ifa_name );
		count++;
	}
	freeifaddrs( list );
	return count;
}

void Sys_InitNetworking( void ) {
	num_interfaces = 0;
	netint[ 0 ].ip = htonl( INADDR_LOOPBACK );
	netint[ 0 ].mask = htonl( 0xFF000000 );
	num_interfaces = 1;
}

void Sys_ShutdownNetworking( void ) {
	num_interfaces = 0;
}

int Sys_MessageBox( const char* title, const char* buffer, messageBoxType_t type ) {
	fprintf( stderr, "\n[%s] %s\n", title != NULL ? title : "Message", buffer != NULL ? buffer : "" );
	return 0;
}

sdLogitechLCDSystem* Sys_GetLogitechLCDSystem( void ) {
	return NULL;
}

void Sys_SetConsoleName( const char* name ) {
}

bool Sys_GetHTTPProxyAddress( char proxy[ MAX_PROXY_LENGTH ] ) {
	return false;
}
