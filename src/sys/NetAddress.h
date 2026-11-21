// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __SYS_NETADDRESS_H__
#define __SYS_NETADDRESS_H__

/*
IPv6 address text and classification, shared by the platform network code
(posix/posix_net.cpp, win32/SystemBootstrap.cpp). Plain C++ without socket headers, so
the asset-free unit suite can check it (tests/NetAddressTest.cpp).

An NA_IP6 netadr_t keeps a 32-bit key in its ip field (NA_Key32), for the code that keys
clients on ip (the game's bans, login throttle and proficiency table; the server's rate
limits): a hash of the address's /64, the block one subscriber is given, in 240.0.0.0/4,
which no IPv4 client can come from. So one IPv6 subscriber counts as one address.
*/

#include <cstdint>
#include <cstring>

// "a:b::c", "::", "::ffff:1.2.3.4" (RFC 4291 section 2.2) into 16 bytes
inline bool NA_ParseIPv6( const char* text, uint8_t out[ 16 ] ) {
	if ( text == NULL ) {
		return false;
	}
	uint16_t groups[ 8 ];
	int numGroups = 0;
	int gap = -1;	// where "::" was
	const char* c = text;
	if ( c[ 0 ] == ':' ) {
		if ( c[ 1 ] != ':' ) {
			return false;
		}
		gap = 0;
		c += 2;
		if ( *c == '\0' ) {
			memset( out, 0, 16 );
			return true;
		}
	}
	while ( *c != '\0' ) {
		// an IPv4 tail: the last 32 bits
		const char* end = c;
		while ( *end != '\0' && *end != ':' ) {
			end++;
		}
		bool dotted = false;
		for ( const char* d = c; d < end; d++ ) {
			dotted = dotted || *d == '.';
		}
		if ( dotted ) {
			if ( *end != '\0' || numGroups > 6 ) {
				return false;
			}
			unsigned int parts[ 4 ];
			int numParts = 0;
			const char* p = c;
			while ( numParts < 4 ) {
				if ( *p < '0' || *p > '9' ) {
					return false;
				}
				unsigned int value = 0;
				int digits = 0;
				while ( *p >= '0' && *p <= '9' ) {
					value = value * 10 + static_cast< unsigned int >( *p - '0' );
					if ( ++digits > 3 || value > 255 ) {
						return false;
					}
					p++;
				}
				parts[ numParts++ ] = value;
				if ( numParts < 4 ) {
					if ( *p != '.' ) {
						return false;
					}
					p++;
				}
			}
			if ( *p != '\0' ) {
				return false;
			}
			groups[ numGroups++ ] = static_cast< uint16_t >( ( parts[ 0 ] << 8 ) | parts[ 1 ] );
			groups[ numGroups++ ] = static_cast< uint16_t >( ( parts[ 2 ] << 8 ) | parts[ 3 ] );
			c = end;
			break;
		}
		const int length = static_cast< int >( end - c );
		if ( length < 1 || length > 4 || numGroups >= 8 ) {
			return false;
		}
		unsigned int value = 0;
		for ( const char* d = c; d < end; d++ ) {
			const char h = *d;
			int digit;
			if ( h >= '0' && h <= '9' ) {
				digit = h - '0';
			} else if ( h >= 'a' && h <= 'f' ) {
				digit = h - 'a' + 10;
			} else if ( h >= 'A' && h <= 'F' ) {
				digit = h - 'A' + 10;
			} else {
				return false;
			}
			value = ( value << 4 ) | static_cast< unsigned int >( digit );
		}
		groups[ numGroups++ ] = static_cast< uint16_t >( value );
		c = end;
		if ( *c == ':' ) {
			c++;
			if ( *c == ':' ) {
				if ( gap >= 0 ) {
					return false;	// two "::"
				}
				gap = numGroups;
				c++;
			} else if ( *c == '\0' ) {
				return false;		// a trailing single ':'
			}
		}
	}
	if ( gap < 0 ? numGroups != 8 : numGroups > 7 ) {
		return false;
	}
	uint16_t full[ 8 ];
	memset( full, 0, sizeof( full ) );
	if ( gap < 0 ) {
		memcpy( full, groups, sizeof( full ) );
	} else {
		for ( int i = 0; i < gap; i++ ) {
			full[ i ] = groups[ i ];
		}
		const int tail = numGroups - gap;
		for ( int i = 0; i < tail; i++ ) {
			full[ 8 - tail + i ] = groups[ gap + i ];
		}
	}
	for ( int i = 0; i < 8; i++ ) {
		out[ i * 2 ] = static_cast< uint8_t >( full[ i ] >> 8 );
		out[ i * 2 + 1 ] = static_cast< uint8_t >( full[ i ] );
	}
	return true;
}

// RFC 5952 text: lower case, the longest run of two or more zero groups as "::"
inline void NA_FormatIPv6( const uint8_t in[ 16 ], char* out, int size ) {
	uint16_t groups[ 8 ];
	for ( int i = 0; i < 8; i++ ) {
		groups[ i ] = static_cast< uint16_t >( ( in[ i * 2 ] << 8 ) | in[ i * 2 + 1 ] );
	}
	int bestStart = -1, bestLength = 0;
	for ( int i = 0; i < 8; ) {
		if ( groups[ i ] != 0 ) {
			i++;
			continue;
		}
		int j = i;
		while ( j < 8 && groups[ j ] == 0 ) {
			j++;
		}
		if ( j - i > bestLength && j - i >= 2 ) {
			bestStart = i;
			bestLength = j - i;
		}
		i = j;
	}
	char text[ 48 ];
	int length = 0;
	for ( int i = 0; i < 8; i++ ) {
		if ( i == bestStart ) {
			text[ length++ ] = ':';
			if ( i == 0 ) {
				text[ length++ ] = ':';
			}
			i += bestLength - 1;
			continue;
		}
		// hex without leading zeros (idLib takes snprintf away)
		static const char digits[] = "0123456789abcdef";
		bool started = false;
		for ( int shift = 12; shift >= 0; shift -= 4 ) {
			const int digit = ( groups[ i ] >> shift ) & 15;
			if ( digit != 0 || started || shift == 0 ) {
				text[ length++ ] = digits[ digit ];
				started = true;
			}
		}
		if ( i < 7 ) {
			text[ length++ ] = ':';
		}
	}
	text[ length ] = '\0';
	if ( size <= 0 ) {
		return;
	}
	const int copy = length < size - 1 ? length : size - 1;
	memcpy( out, text, copy );
	out[ copy ] = '\0';
}

/*
Splits "host:port", "[v6]:port", "[v6]", a bare IPv6 address (two or more colons, so no
port) or "host" into host and port (0 when none). An IPv6 zone ("fe80::1%eth0") stays in
the host. False on a malformed bracket or port.
*/
inline bool NA_SplitHostPort( const char* text, char* host, int hostSize, int* port ) {
	*port = 0;
	if ( text == NULL || hostSize <= 0 ) {
		return false;
	}
	const char* portText = NULL;
	int hostLength;
	const char* hostStart = text;
	if ( text[ 0 ] == '[' ) {
		const char* close = strchr( text, ']' );
		if ( close == NULL ) {
			return false;
		}
		hostStart = text + 1;
		hostLength = static_cast< int >( close - hostStart );
		if ( close[ 1 ] == ':' ) {
			portText = close + 2;
		} else if ( close[ 1 ] != '\0' ) {
			return false;
		}
	} else {
		const char* first = strchr( text, ':' );
		const char* last = strrchr( text, ':' );
		if ( first != NULL && first == last ) {
			hostLength = static_cast< int >( first - text );
			portText = first + 1;
		} else {
			hostLength = static_cast< int >( strlen( text ) );
		}
	}
	if ( hostLength <= 0 || hostLength >= hostSize ) {
		return false;
	}
	memcpy( host, hostStart, hostLength );
	host[ hostLength ] = '\0';
	if ( portText != NULL ) {
		int value = 0;
		if ( *portText == '\0' ) {
			return false;
		}
		for ( const char* p = portText; *p != '\0'; p++ ) {
			if ( *p < '0' || *p > '9' ) {
				return false;
			}
			value = value * 10 + ( *p - '0' );
			if ( value > 65535 ) {
				return false;
			}
		}
		*port = value;
	}
	return true;
}

inline bool NA_IsV4Mapped( const uint8_t ip[ 16 ] ) {
	static const uint8_t prefix[ 12 ] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
	return memcmp( ip, prefix, 12 ) == 0;
}

inline bool NA_IsLoopbackIPv6( const uint8_t ip[ 16 ] ) {
	static const uint8_t loopback[ 16 ] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
	return memcmp( ip, loopback, 16 ) == 0;
}

inline bool NA_IsLinkLocalIPv6( const uint8_t ip[ 16 ] ) {
	return ip[ 0 ] == 0xfe && ( ip[ 1 ] & 0xc0 ) == 0x80;		// fe80::/10
}

// on the local network: link-local, unique local (fc00::/7) or loopback
inline bool NA_IsLANIPv6( const uint8_t ip[ 16 ] ) {
	return NA_IsLinkLocalIPv6( ip ) || ( ip[ 0 ] & 0xfe ) == 0xfc || NA_IsLoopbackIPv6( ip );
}

// a global unicast address (2000::/3): one the Internet can reach
inline bool NA_IsGlobalIPv6( const uint8_t ip[ 16 ] ) {
	return ( ip[ 0 ] & 0xe0 ) == 0x20;
}

// the module comment: a key in 240.0.0.0/4 from the address's /64
inline void NA_Key32( const uint8_t ip6[ 16 ], uint8_t key[ 4 ] ) {
	uint32_t hash = 2166136261u;
	for ( int i = 0; i < 8; i++ ) {
		hash = ( hash ^ ip6[ i ] ) * 16777619u;
	}
	key[ 0 ] = static_cast< uint8_t >( 0xf0 | ( hash >> 28 ) );
	key[ 1 ] = static_cast< uint8_t >( hash >> 16 );
	key[ 2 ] = static_cast< uint8_t >( hash >> 8 );
	key[ 3 ] = static_cast< uint8_t >( hash );
}

#endif /* !__SYS_NETADDRESS_H__ */
