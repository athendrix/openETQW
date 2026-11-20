// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __ASYNC_SERVERQUERY_H__
#define __ASYNC_SERVERQUERY_H__

/*
The server-list query: a client asks a server for its settings and players without
connecting, for the menu's server browser (src/sdnet/SDNetBrowser.cpp).

  request   short -1 (connectionless), string "getInfo", long protocol, long nonce
  response  short -1, string "infoResponse", long protocol, long nonce,
            short key count, then each key and value as strings,
            byte player count, then each player's name (string), ping (short), bot (byte)

The nonce ties a response to its request (and a LAN broadcast's answers to that
broadcast). A server answers only requests with its own protocol, and no more often than
sqRateLimiter allows: the response is larger than the request, so an unlimited server
could be used to flood a spoofed address with traffic.

Everything here reads and writes through idBitMsg only (no idDict), so the asset-free
unit suite can test it (tests/ServerQueryTest.cpp).
*/

#include <cstring>

const int SQ_MAX_KEYS			= 64;
const int SQ_MAX_KEY_LENGTH		= 64;
const int SQ_MAX_VALUE_LENGTH	= 256;
const int SQ_MAX_PLAYERS		= 64;
const int SQ_MAX_NAME_LENGTH	= 40;

static const char SQ_REQUEST[]	= "getInfo";
static const char SQ_RESPONSE[]	= "infoResponse";

struct sqPlayer_t {
	char	name[ SQ_MAX_NAME_LENGTH ];
	int		ping;
	bool	bot;
};

struct sqInfo_t {
	int			protocol;
	int			nonce;
	int			numKeys;
	char		keys[ SQ_MAX_KEYS ][ SQ_MAX_KEY_LENGTH ];
	char		values[ SQ_MAX_KEYS ][ SQ_MAX_VALUE_LENGTH ];
	int			numPlayers;
	sqPlayer_t	players[ SQ_MAX_PLAYERS ];

	void Clear() {
		memset( this, 0, sizeof( *this ) );
	}

	// adds a key; false when the table is full
	bool AddKey( const char* key, const char* value ) {
		if ( numKeys >= SQ_MAX_KEYS || key == NULL || key[ 0 ] == '\0' ) {
			return false;
		}
		CopyString( keys[ numKeys ], key, SQ_MAX_KEY_LENGTH );
		CopyString( values[ numKeys ], value != NULL ? value : "", SQ_MAX_VALUE_LENGTH );
		numKeys++;
		return true;
	}

	bool AddPlayer( const char* name, int ping, bool bot ) {
		if ( numPlayers >= SQ_MAX_PLAYERS ) {
			return false;
		}
		sqPlayer_t& player = players[ numPlayers++ ];
		CopyString( player.name, name != NULL ? name : "", SQ_MAX_NAME_LENGTH );
		player.ping = ping < 0 ? 0 : ( ping > 9999 ? 9999 : ping );
		player.bot = bot;
		return true;
	}

	const char* FindValue( const char* key ) const {
		for ( int i = 0; i < numKeys; i++ ) {
			if ( strcmp( keys[ i ], key ) == 0 ) {
				return values[ i ];
			}
		}
		return NULL;
	}

	static void CopyString( char* dest, const char* src, int size ) {
		strncpy( dest, src, size - 1 );
		dest[ size - 1 ] = '\0';
	}
};

// the request after its command string
inline void SQ_WriteRequest( idBitMsg& msg, int protocol, int nonce ) {
	msg.WriteShort( -1 );	// CONNECTIONLESS_MESSAGE_ID
	msg.WriteString( SQ_REQUEST );
	msg.WriteLong( protocol );
	msg.WriteLong( nonce );
}

// reads a request after its command string; false when it is too short
inline bool SQ_ReadRequest( const idBitMsg& msg, int& protocol, int& nonce ) {
	if ( msg.GetRemainingData() < 8 ) {
		return false;
	}
	protocol = msg.ReadLong();
	nonce = msg.ReadLong();
	return true;
}

/*
Writes a response into what is left of msg. The players go in whole if they fit; then
the keys, in the caller's order, while they fit (callers add the keys the browser shows
first); then as many players as fit. Callers size msg below the path MTU (about 1400
bytes) so the answer is one unfragmented packet.
*/
inline int SQ_PlayerBytes( const sqPlayer_t& player ) {
	return static_cast< int >( strlen( player.name ) ) + 1 + 2 + 1;
}

inline void SQ_WriteResponse( idBitMsg& msg, const sqInfo_t& info ) {
	msg.WriteShort( -1 );
	msg.WriteString( SQ_RESPONSE );
	msg.WriteLong( info.protocol );
	msg.WriteLong( info.nonce );

	int playerBytes = 1;
	for ( int i = 0; i < info.numPlayers; i++ ) {
		playerBytes += SQ_PlayerBytes( info.players[ i ] );
	}
	int numKeys = 0;
	int keyBytes = 2;
	for ( ; numKeys < info.numKeys; numKeys++ ) {
		const int size = static_cast< int >( strlen( info.keys[ numKeys ] ) + strlen( info.values[ numKeys ] ) ) + 2;
		if ( keyBytes + size + playerBytes > msg.GetRemainingSpace() ) {
			break;
		}
		keyBytes += size;
	}
	msg.WriteShort( numKeys );
	for ( int i = 0; i < numKeys; i++ ) {
		msg.WriteString( info.keys[ i ], -1, false );
		msg.WriteString( info.values[ i ], -1, false );
	}

	int numPlayers = 0;
	int room = msg.GetRemainingSpace() - 1;
	while ( numPlayers < info.numPlayers && SQ_PlayerBytes( info.players[ numPlayers ] ) <= room ) {
		room -= SQ_PlayerBytes( info.players[ numPlayers ] );
		numPlayers++;
	}
	msg.WriteByte( numPlayers );
	for ( int i = 0; i < numPlayers; i++ ) {
		msg.WriteString( info.players[ i ].name, -1, false );
		msg.WriteShort( info.players[ i ].ping );
		msg.WriteByte( info.players[ i ].bot ? 1 : 0 );
	}
}

/*
Reads a response after its command string. Counts above the limits, and a message that
ends early, fail; strings longer than the limits are cut.
*/
inline bool SQ_ReadResponse( const idBitMsg& msg, sqInfo_t& info ) {
	info.Clear();
	if ( msg.GetRemainingData() < 10 ) {
		return false;
	}
	info.protocol = msg.ReadLong();
	info.nonce = msg.ReadLong();
	const int numKeys = msg.ReadShort();
	if ( numKeys < 0 || numKeys > SQ_MAX_KEYS ) {
		return false;
	}
	for ( int i = 0; i < numKeys; i++ ) {
		if ( msg.GetRemainingData() < 2 ) {
			return false;
		}
		msg.ReadString( info.keys[ i ], SQ_MAX_KEY_LENGTH );
		msg.ReadString( info.values[ i ], SQ_MAX_VALUE_LENGTH );
	}
	info.numKeys = numKeys;
	if ( msg.GetRemainingData() < 1 ) {
		return false;
	}
	const int numPlayers = msg.ReadByte();
	if ( numPlayers > SQ_MAX_PLAYERS ) {
		return false;
	}
	for ( int i = 0; i < numPlayers; i++ ) {
		if ( msg.GetRemainingData() < 4 ) {
			return false;
		}
		sqPlayer_t& player = info.players[ i ];
		msg.ReadString( player.name, SQ_MAX_NAME_LENGTH );
		player.ping = msg.ReadShort();
		player.bot = msg.ReadByte() != 0;
	}
	info.numPlayers = numPlayers;
	return true;
}

/*
How often a server answers queries: each address at most once per minIntervalMsec, and
all addresses together at most maxPerSecond times a second. A browser sends one query per
server per refresh (and retries two), so neither limit is reached by real players.
*/
class sqRateLimiter {
public:
	sqRateLimiter( int minIntervalMsec = 100, int maxPerSecond = 60 ) :
		minInterval( minIntervalMsec ), maxPerSecond( maxPerSecond ), windowStart( 0 ), windowCount( 0 ), next( 0 ) {
		memset( entries, 0, sizeof( entries ) );
	}

	bool Allow( unsigned int address, int now ) {
		if ( windowCount == 0 || now - windowStart >= 1000 || now < windowStart ) {
			windowStart = now;
			windowCount = 0;
		}
		if ( windowCount >= maxPerSecond ) {
			return false;
		}
		entry_t* slot = NULL;
		for ( int i = 0; i < NUM_ENTRIES; i++ ) {
			if ( entries[ i ].used && entries[ i ].address == address ) {
				slot = &entries[ i ];
				break;
			}
		}
		if ( slot != NULL ) {
			if ( now >= slot->time && now - slot->time < minInterval ) {
				return false;
			}
		} else {
			slot = &entries[ next ];
			next = ( next + 1 ) % NUM_ENTRIES;
		}
		slot->used = true;
		slot->address = address;
		slot->time = now;
		windowCount++;
		return true;
	}

private:
	static const int NUM_ENTRIES = 64;
	struct entry_t {
		bool			used;
		unsigned int	address;
		int				time;
	};
	entry_t	entries[ NUM_ENTRIES ];
	int		minInterval;
	int		maxPerSecond;
	int		windowStart;
	int		windowCount;
	int		next;
};

#endif /* !__ASYNC_SERVERQUERY_H__ */
