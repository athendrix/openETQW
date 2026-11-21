// SPDX-License-Identifier: GPL-3.0-or-later
/*
The engine's DHT node (Dht.h): jech/dht (src/libs/dht) on one of the game's ports.

jech/dht asks its user for sending, hashing, random bytes and a blacklist; they are
here. It keeps no sockets of its own: dht_init gets two placeholder descriptors that
only say which address families are on, and dht_sendto sends through the idPort the
node runs on.

The node's id and the good nodes it knows are kept in the save path (dht.dat), so a
restart rejoins the DHT at once; the first start, or one whose nodes all went away,
asks the well-known bootstrap routers, looked up on a thread so the frame never waits
on DNS.
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "Dht.h"
#include "../framework/async/Sha256.h"
#include "../framework/async/SecureRandom.h"
#include "../sys/NetAddress.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>
#include <vector>

#include "../libs/dht/dht.h"

idCVar net_dht( "net_dht", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT, "find Internet servers through the BitTorrent DHT (the server browser's Internet tab)" );
idCVar net_dhtRouters( "net_dhtRouters", "1", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "ask the public DHT routers for nodes when the node knows too few (off for a private DHT; dhtPing adds nodes)" );
idCVar net_dhtReadyNodes( "net_dhtReadyNodes", "4", CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT, "good DHT nodes known before the node announces or searches (tests lower it)", 1, 64 );
extern idCVar net_ipv6;

namespace {

// SHA-1( "Enemy Territory Quake Wars" )
const unsigned char DHT_INFO_HASH[ 20 ] = {
	0x0a, 0xb9, 0x9a, 0xa7, 0x3a, 0x8b, 0x82, 0xee, 0xd6, 0x7a,
	0x9d, 0x00, 0x48, 0x94, 0x0a, 0x9c, 0xb4, 0x23, 0xe3, 0x6a
};
const unsigned char DHT_VERSION[ 4 ] = { 'O', 'E', 0, 1 };

const char DHT_FILE[]				= "dht.dat";
const char DHT_FILE_MAGIC[ 8 ]		= { 'O', 'E', 'D', 'H', 'T', '1', 0, 0 };
const int DHT_MAX_SAVED_NODES		= 300;
const int DHT_SAVE_SECONDS			= 10 * 60;
const int DHT_ANNOUNCE_SECONDS		= 15 * 60;
const int DHT_SEARCH_MIN_SECONDS	= 60;		// a new search at most this often
const int DHT_ENOUGH_NODES			= 8;		// good nodes before the routers are asked
const int DHT_SOCKET4				= 4;		// placeholders (the module comment)
const int DHT_SOCKET6				= 6;

const char* const DHT_ROUTERS[] = {
	"router.bittorrent.com:6881",
	"dht.transmissionbt.com:6881",
	"router.utorrent.com:6881",
	"dht.libtorrent.org:25401",
};

unsigned char myId[ 20 ];		// this node's id, kept in dht.dat

struct dhtState_t {
	idPort*			port;
	bool			ipv6;
	time_t			nextPeriodic;
	time_t			nextSave;
	time_t			nextAnnounce;
	time_t			lastSearch;
	bool			searchWanted;
	int				announcePort;
	dhtPeerFunc_t	peerFunc;
	int				peersFound;

	// the routers' addresses, from the lookup thread
	std::mutex				routerLock;
	std::vector< netadr_t >	routers;
	bool					routersDone;
	bool					routersAsked;
};

dhtState_t& State() {
	static dhtState_t* state = NULL;
	if ( state == NULL ) {
		state = new dhtState_t();
		state->port = NULL;
		state->ipv6 = false;
		state->nextPeriodic = 0;
		state->nextSave = 0;
		state->nextAnnounce = 0;
		state->lastSearch = 0;
		state->searchWanted = false;
		state->announcePort = 0;
		state->peerFunc = NULL;
		state->peersFound = 0;
		state->routersDone = false;
		state->routersAsked = false;
	}
	return *state;
}

bool NetadrToSockaddr( const netadr_t& address, sockaddr_storage& out, int& size ) {
	memset( &out, 0, sizeof( out ) );
	if ( address.type == NA_IP ) {
		sockaddr_in& in = reinterpret_cast< sockaddr_in& >( out );
		in.sin_family = AF_INET;
		in.sin_port = htons( address.port );
		memcpy( &in.sin_addr, address.ip, 4 );
		size = sizeof( in );
		return true;
	}
	if ( address.type == NA_IP6 ) {
		sockaddr_in6& in6 = reinterpret_cast< sockaddr_in6& >( out );
		in6.sin6_family = AF_INET6;
		in6.sin6_port = htons( address.port );
		memcpy( &in6.sin6_addr, address.ip6, 16 );
		in6.sin6_scope_id = address.scope;
		size = sizeof( in6 );
		return true;
	}
	return false;
}

bool SockaddrToNetadr( const sockaddr* address, int size, netadr_t& out ) {
	memset( &out, 0, sizeof( out ) );
	if ( address == NULL ) {
		return false;
	}
	if ( address->sa_family == AF_INET && size >= static_cast< int >( sizeof( sockaddr_in ) ) ) {
		const sockaddr_in* in = reinterpret_cast< const sockaddr_in* >( address );
		out.type = NA_IP;
		memcpy( out.ip, &in->sin_addr, 4 );
		out.port = ntohs( in->sin_port );
		return true;
	}
	if ( address->sa_family == AF_INET6 && size >= static_cast< int >( sizeof( sockaddr_in6 ) ) ) {
		const sockaddr_in6* in6 = reinterpret_cast< const sockaddr_in6* >( address );
		const uint8_t* bytes = reinterpret_cast< const uint8_t* >( &in6->sin6_addr );
		if ( NA_IsV4Mapped( bytes ) ) {
			out.type = NA_IP;
			memcpy( out.ip, bytes + 12, 4 );
		} else {
			out.type = NA_IP6;
			memcpy( out.ip6, bytes, 16 );
			out.scope = NA_IsLinkLocalIPv6( bytes ) ? in6->sin6_scope_id : 0;
			NA_Key32( bytes, out.ip );
		}
		out.port = ntohs( in6->sin6_port );
		return true;
	}
	return false;
}

// jech/dht's results: compact peers, 6 bytes (IPv4) or 18 (IPv6) each
void Callback( void*, int event, const unsigned char*, const void* data, size_t size ) {
	dhtState_t& state = State();
	const int each = event == DHT_EVENT_VALUES ? 6 : event == DHT_EVENT_VALUES6 ? 18 : 0;
	if ( each == 0 || data == NULL ) {
		return;
	}
	const unsigned char* bytes = static_cast< const unsigned char* >( data );
	for ( size_t at = 0; at + each <= size; at += each ) {
		netadr_t peer;
		memset( &peer, 0, sizeof( peer ) );
		if ( each == 6 ) {
			peer.type = NA_IP;
			memcpy( peer.ip, bytes + at, 4 );
			peer.port = static_cast< unsigned short >( ( bytes[ at + 4 ] << 8 ) | bytes[ at + 5 ] );
		} else {
			if ( NA_IsV4Mapped( bytes + at ) || NA_IsLinkLocalIPv6( bytes + at ) ) {
				continue;
			}
			peer.type = NA_IP6;
			memcpy( peer.ip6, bytes + at, 16 );
			NA_Key32( peer.ip6, peer.ip );
			peer.port = static_cast< unsigned short >( ( bytes[ at + 16 ] << 8 ) | bytes[ at + 17 ] );
		}
		if ( peer.port == 0 ) {
			continue;
		}
		state.peersFound++;
		if ( state.peerFunc != NULL ) {
			state.peerFunc( peer );
		}
	}
}

void Periodic( const void* data, int size, const netadr_t* from ) {
	dhtState_t& state = State();
	time_t sleep = 1;
	if ( data != NULL && from != NULL ) {
		sockaddr_storage address;
		int addressSize = 0;
		if ( !NetadrToSockaddr( *from, address, addressSize ) ) {
			return;
		}
		// jech/dht wants a NUL after the packet
		static char buffer[ MAX_MESSAGE_SIZE + 1 ];
		const int length = Min( size, static_cast< int >( MAX_MESSAGE_SIZE ) );
		memcpy( buffer, data, length );
		buffer[ length ] = '\0';
		dht_periodic( buffer, length, reinterpret_cast< sockaddr* >( &address ), addressSize, &sleep, Callback, NULL );
	} else {
		dht_periodic( NULL, 0, NULL, 0, &sleep, Callback, NULL );
	}
	state.nextPeriodic = time( NULL ) + Max( static_cast< time_t >( 0 ), sleep );
}

int GoodNodes() {
	int good = 0, dubious = 0;
	int total = 0;
	dht_nodes( AF_INET, &good, &dubious, NULL, NULL );
	total += good;
	if ( State().ipv6 ) {
		good = 0;
		dht_nodes( AF_INET6, &good, &dubious, NULL, NULL );
		total += good;
	}
	return total;
}

void LookUpRouters() {
	std::vector< netadr_t > found;
	for ( size_t r = 0; r < sizeof( DHT_ROUTERS ) / sizeof( DHT_ROUTERS[ 0 ] ); r++ ) {
		char host[ 128 ];
		int port = 0;
		if ( !NA_SplitHostPort( DHT_ROUTERS[ r ], host, sizeof( host ), &port ) ) {
			continue;
		}
		addrinfo hints;
		memset( &hints, 0, sizeof( hints ) );
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;
		addrinfo* results = NULL;
		if ( getaddrinfo( host, NULL, &hints, &results ) != 0 ) {
			continue;
		}
		for ( addrinfo* result = results; result != NULL; result = result->ai_next ) {
			netadr_t address;
			if ( SockaddrToNetadr( result->ai_addr, static_cast< int >( result->ai_addrlen ), address ) ) {
				address.port = static_cast< unsigned short >( port );
				found.push_back( address );
			}
		}
		freeaddrinfo( results );
	}
	dhtState_t& state = State();
	std::lock_guard< std::mutex > hold( state.routerLock );
	state.routers = found;
	state.routersDone = true;
}

void Ping( const netadr_t& address ) {
	sockaddr_storage socketAddress;
	int size = 0;
	if ( NetadrToSockaddr( address, socketAddress, size ) && ( address.type == NA_IP || State().ipv6 ) ) {
		dht_ping_node( reinterpret_cast< sockaddr* >( &socketAddress ), size );
	}
}

/*
dht.dat: DHT_FILE_MAGIC, the 20-byte id, then each node as a type byte (4 or 6), 16
address bytes (IPv4 in the first 4) and the port, big-endian.
*/
bool LoadNodes( unsigned char id[ 20 ], std::vector< netadr_t >& nodes ) {
	if ( fileSystem == NULL ) {
		return false;
	}
	idFile* file = fileSystem->OpenFileRead( DHT_FILE );
	if ( file == NULL ) {
		return false;
	}
	char magic[ 8 ];
	bool ok = file->Read( magic, 8 ) == 8 && memcmp( magic, DHT_FILE_MAGIC, 8 ) == 0 && file->Read( id, 20 ) == 20;
	unsigned char record[ 19 ];
	while ( ok && nodes.size() < static_cast< size_t >( DHT_MAX_SAVED_NODES ) && file->Read( record, sizeof( record ) ) == static_cast< int >( sizeof( record ) ) ) {
		netadr_t node;
		memset( &node, 0, sizeof( node ) );
		node.port = static_cast< unsigned short >( ( record[ 17 ] << 8 ) | record[ 18 ] );
		if ( record[ 0 ] == 4 ) {
			node.type = NA_IP;
			memcpy( node.ip, record + 1, 4 );
		} else if ( record[ 0 ] == 6 ) {
			node.type = NA_IP6;
			memcpy( node.ip6, record + 1, 16 );
			NA_Key32( node.ip6, node.ip );
		} else {
			continue;
		}
		nodes.push_back( node );
	}
	fileSystem->CloseFile( file );
	return ok;
}

void SaveNodes() {
	if ( fileSystem == NULL ) {
		return;
	}
	static sockaddr_in nodes4[ DHT_MAX_SAVED_NODES ];
	static sockaddr_in6 nodes6[ DHT_MAX_SAVED_NODES ];
	int num4 = DHT_MAX_SAVED_NODES, num6 = State().ipv6 ? DHT_MAX_SAVED_NODES : 0;
	if ( dht_get_nodes( nodes4, &num4, nodes6, &num6 ) < 0 ) {
		return;
	}
	idFile* file = fileSystem->OpenFileWrite( DHT_FILE );
	if ( file == NULL ) {
		return;
	}
	file->Write( DHT_FILE_MAGIC, 8 );
	file->Write( myId, 20 );
	for ( int i = 0; i < num4; i++ ) {
		unsigned char record[ 19 ];
		memset( record, 0, sizeof( record ) );
		record[ 0 ] = 4;
		memcpy( record + 1, &nodes4[ i ].sin_addr, 4 );
		const unsigned short port = ntohs( nodes4[ i ].sin_port );
		record[ 17 ] = static_cast< unsigned char >( port >> 8 );
		record[ 18 ] = static_cast< unsigned char >( port );
		file->Write( record, sizeof( record ) );
	}
	for ( int i = 0; i < num6; i++ ) {
		unsigned char record[ 19 ];
		record[ 0 ] = 6;
		memcpy( record + 1, &nodes6[ i ].sin6_addr, 16 );
		const unsigned short port = ntohs( nodes6[ i ].sin6_port );
		record[ 17 ] = static_cast< unsigned char >( port >> 8 );
		record[ 18 ] = static_cast< unsigned char >( port );
		file->Write( record, sizeof( record ) );
	}
	fileSystem->CloseFile( file );
}

void StartSearches( int port ) {
	dhtState_t& state = State();
	dht_search( DHT_INFO_HASH, port, AF_INET, Callback, NULL );
	if ( state.ipv6 ) {
		dht_search( DHT_INFO_HASH, port, AF_INET6, Callback, NULL );
	}
	state.lastSearch = time( NULL );
	state.searchWanted = false;
}

void DhtStatus_f( const idCmdArgs& ) {
	dhtState_t& state = State();
	if ( state.port == NULL ) {
		common->Printf( "DHT: not running (net_dht %d)\n", net_dht.GetInteger() );
		return;
	}
	int good = 0, dubious = 0, cached = 0, incoming = 0;
	dht_nodes( AF_INET, &good, &dubious, &cached, &incoming );
	common->Printf( "DHT on port %d: IPv4 %d good, %d dubious, %d cached, %d incoming\n", state.port->GetPort(), good, dubious, cached, incoming );
	if ( state.ipv6 ) {
		good = dubious = cached = incoming = 0;
		dht_nodes( AF_INET6, &good, &dubious, &cached, &incoming );
		common->Printf( "        IPv6 %d good, %d dubious, %d cached, %d incoming\n", good, dubious, cached, incoming );
	}
	common->Printf( "  announcing: %s; servers found: %d\n", state.announcePort != 0 ? va( "port %d", state.announcePort ) : "no", state.peersFound );
}

} // namespace

/*
================
jech/dht's user functions
================
*/
extern "C" int dht_sendto( int, const void* buf, int len, int, const struct sockaddr* to, int tolen ) {
	dhtState_t& state = State();
	netadr_t address;
	if ( state.port == NULL || !SockaddrToNetadr( to, tolen, address ) ) {
		return -1;
	}
	state.port->SendPacket( address, buf, len );
	return len;
}

extern "C" int dht_blacklisted( const struct sockaddr*, int ) {
	return 0;
}

extern "C" void dht_hash( void* hash_return, int hash_size, const void* v1, int len1, const void* v2, int len2, const void* v3, int len3 ) {
	sqSha256 sha;
	if ( v1 != NULL && len1 > 0 ) {
		sha.Update( v1, len1 );
	}
	if ( v2 != NULL && len2 > 0 ) {
		sha.Update( v2, len2 );
	}
	if ( v3 != NULL && len3 > 0 ) {
		sha.Update( v3, len3 );
	}
	uint8_t digest[ sqSha256::DIGEST_SIZE ];
	sha.Final( digest );
	memset( hash_return, 0, hash_size );
	memcpy( hash_return, digest, Min( hash_size, static_cast< int >( sqSha256::DIGEST_SIZE ) ) );
}

extern "C" int dht_random_bytes( void* buf, size_t size ) {
	unsigned char* out = static_cast< unsigned char* >( buf );
	for ( size_t i = 0; i < size; i += 4 ) {
		const uint32_t value = Sys_SecureRandom32();
		for ( size_t j = 0; j < 4 && i + j < size; j++ ) {
			out[ i + j ] = static_cast< unsigned char >( value >> ( 8 * j ) );
		}
	}
	return static_cast< int >( size );
}

#if defined( _WIN32 ) && !defined( __MINGW32__ )
extern "C" int dht_gettimeofday( struct timeval* tv, struct timezone* ) {
	FILETIME now;
	GetSystemTimeAsFileTime( &now );
	const unsigned long long ticks = ( static_cast< unsigned long long >( now.dwHighDateTime ) << 32 ) | now.dwLowDateTime;
	const unsigned long long micros = ticks / 10 - 11644473600000000ULL;	// since 1970
	tv->tv_sec = static_cast< long >( micros / 1000000 );
	tv->tv_usec = static_cast< long >( micros % 1000000 );
	return 0;
}
#endif

/*
================
DHT_*
================
*/
bool DHT_Running( void ) {
	return State().port != NULL;
}

bool DHT_RunningOn( const idPort* port ) {
	return port != NULL && State().port == port;
}

bool DHT_Start( idPort* port ) {
	dhtState_t& state = State();
	if ( state.port != NULL ) {
		return state.port == port;
	}
	if ( port == NULL || port->GetPort() == 0 || !net_dht.GetBool() ) {
		return false;
	}
	std::vector< netadr_t > saved;
	if ( !LoadNodes( myId, saved ) ) {
		dht_random_bytes( myId, sizeof( myId ) );
		saved.clear();
	}

	// jech/dht's random() and its Windows stand-in rand()
	const unsigned int seed = Sys_SecureRandom32();
#ifdef _WIN32
	srand( seed );
#else
	srandom( seed );
#endif
	state.ipv6 = net_ipv6.GetBool();
	if ( dht_init( DHT_SOCKET4, state.ipv6 ? DHT_SOCKET6 : -1, myId, DHT_VERSION ) < 0 ) {
		common->Warning( "DHT: couldn't start" );
		return false;
	}
	state.port = port;
	const time_t now = time( NULL );
	state.nextPeriodic = now;
	state.nextSave = now + DHT_SAVE_SECONDS;
	state.nextAnnounce = now;
	for ( size_t i = 0; i < saved.size(); i++ ) {
		Ping( saved[ i ] );
	}
	common->Printf( "DHT: started on port %d, %d saved nodes\n", port->GetPort(), static_cast< int >( saved.size() ) );
	return true;
}

void DHT_Stop( idPort* port ) {
	dhtState_t& state = State();
	if ( state.port == NULL || state.port != port ) {
		return;
	}
	SaveNodes();
	dht_uninit();
	state.port = NULL;
	state.announcePort = 0;
	state.searchWanted = false;
}

bool DHT_IsPacket( const void* data, int size ) {
	const unsigned char* bytes = static_cast< const unsigned char* >( data );
	return size >= 3 && bytes[ 0 ] == 'd' && bytes[ 1 ] >= '1' && bytes[ 1 ] <= '9' && ( bytes[ 2 ] == ':' || ( bytes[ 2 ] >= '0' && bytes[ 2 ] <= '9' ) );
}

void DHT_Input( const netadr_t& from, const void* data, int size ) {
	if ( State().port == NULL || data == NULL || size <= 0 ) {
		return;
	}
	Periodic( data, size, &from );
}

void DHT_Frame( void ) {
	dhtState_t& state = State();
	if ( state.port == NULL ) {
		return;
	}
	const time_t now = time( NULL );

	// the routers, when we know too few nodes
	if ( !state.routersAsked && net_dhtRouters.GetBool() && GoodNodes() < DHT_ENOUGH_NODES && now >= state.nextSave - DHT_SAVE_SECONDS + 3 ) {
		state.routersAsked = true;
		std::thread( LookUpRouters ).detach();
	}
	{
		std::lock_guard< std::mutex > hold( state.routerLock );
		if ( state.routersDone ) {
			for ( size_t i = 0; i < state.routers.size(); i++ ) {
				Ping( state.routers[ i ] );
			}
			state.routers.clear();
			state.routersDone = false;
		}
	}

	if ( now >= state.nextPeriodic ) {
		Periodic( NULL, 0, NULL );
	}
	const bool ready = GoodNodes() >= net_dhtReadyNodes.GetInteger();
	if ( ready && state.announcePort != 0 && now >= state.nextAnnounce ) {
		StartSearches( state.announcePort );
		state.nextAnnounce = now + DHT_ANNOUNCE_SECONDS;
	} else if ( state.searchWanted && ( ready || now - state.lastSearch > 20 ) && now - state.lastSearch >= DHT_SEARCH_MIN_SECONDS ) {
		StartSearches( state.announcePort );
	}
	if ( now >= state.nextSave ) {
		SaveNodes();
		state.nextSave = now + DHT_SAVE_SECONDS;
	}
}

void DHT_SetAnnouncePort( int port ) {
	dhtState_t& state = State();
	if ( state.announcePort != port ) {
		state.announcePort = port;
		state.nextAnnounce = time( NULL );
	}
}

void DHT_Search( void ) {
	State().searchWanted = true;
}

void DHT_SetPeerFunc( dhtPeerFunc_t func ) {
	State().peerFunc = func;
}

void DHT_AddNode( const netadr_t& address ) {
	if ( State().port != NULL ) {
		Ping( address );
	}
}

static void DhtPing_f( const idCmdArgs& args ) {
	netadr_t address;
	if ( args.Argc() != 2 || !Sys_StringToNetAdr( args.Argv( 1 ), &address, true ) || !NetAdr_IsIP( address ) || address.port == 0 ) {
		common->Printf( "usage: dhtPing <host:port>: asks a DHT node to join (a private DHT, or when the routers can't be reached)\n" );
		return;
	}
	DHT_AddNode( address );
}

void DHT_RegisterCommands( void ) {
	cmdSystem->AddCommand( "dhtStatus", DhtStatus_f, CMD_FL_SYSTEM, "prints the DHT node's state (server discovery)" );
	cmdSystem->AddCommand( "dhtPing", DhtPing_f, CMD_FL_SYSTEM, "asks a DHT node at host:port to join" );
}
