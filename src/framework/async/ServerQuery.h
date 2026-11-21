// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __ASYNC_SERVERQUERY_H__
#define __ASYNC_SERVERQUERY_H__

/*
The server-list query: a client asks a server for its settings and players without
connecting, for the menu's server browser (src/sdnet/SDNetBrowser.cpp). Every message
starts with short -1 (connectionless) and a command string.

  1. client  "getInfoChallenge"  long protocol, long nonce, SQ_CHALLENGE_PAD zero bytes
  2. server  "infoChallenge"     long protocol, long nonce, long issued, byte difficulty,
                                 SQ_COOKIE_SIZE bytes cookie
  3. client  "getInfo"           long protocol, long nonce, long issued, byte difficulty,
                                 cookie, 8 bytes work, and optionally byte 1: prove the
                                 server's identity (ServerIdentity.h)
  4. server  "infoResponse"      long protocol, long nonce, short key count, each key and
                                 value as strings, byte player count, each player's name
                                 (string), ping (short) and bot flag (byte), then the
                                 match's state (byte, the game's PGS_* flags) and its time
                                 left in milliseconds (long)

Step 1 is padded so the server's answer to it is no bigger than the question: a spoofed
source gets nothing amplified. The cookie is an HMAC of the client's address and port,
the nonce, when it was issued and the difficulty, under a secret the server never sends,
so the server keeps no state per client and a client that did not receive the cookie
(a spoofed address) cannot make step 3. Step 3 also proves work: SHA-256 of the cookie
and the 8 work bytes must start with `difficulty` zero bits, about 2^difficulty hashes
for the client against one for the server (sqQueryGate). A server raises the difficulty
while it is flooded. A cookie is good for SQ_COOKIE_LIFETIME seconds and answers once.

The nonce ties every answer to its question (and a LAN broadcast's answers to that
broadcast). A server answers only its own protocol, and the info answers are further
limited per address and in all by sqRateLimiter.

Everything here is plain C++ over idBitMsg (no idDict, no engine state), so the asset-free
unit suite can test it (tests/ServerQueryTest.cpp).
*/

#include <cstdint>
#include <cstring>
#include "Sha256.h"

static const char SQ_CHALLENGE_REQUEST[]	= "getInfoChallenge";
static const char SQ_CHALLENGE[]			= "infoChallenge";
const int SQ_COOKIE_SIZE		= 16;
const int SQ_CHALLENGE_PAD		= 48;	// step 1 is 2 + 17 + 8 + 48 = 75 bytes; step 2 is 2 + 14 + 13 + 16 = 45
const int SQ_COOKIE_LIFETIME	= 30;	// seconds
const int SQ_MIN_DIFFICULTY		= 8;
const int SQ_MAX_DIFFICULTY		= 28;
const int SQ_ADDRESS_SIZE		= 18;	// 16 bytes of IPv6 (IPv4 mapped as ::ffff:a.b.c.d) and the port


const int SQ_MAX_KEYS			= 64;
const int SQ_MAX_KEY_LENGTH		= 64;
const int SQ_MAX_VALUE_LENGTH	= 256;
const int SQ_MAX_PLAYERS		= 64;
const int SQ_MAX_NAME_LENGTH	= 40;

static const char SQ_REQUEST[]	= "getInfo";	// step 3
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
	int			gameState;		// idGame::GetProbeState: warmup, running, reviewing, loading
	int			timeLeft;		// idGame::GetProbeTime, milliseconds

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

// the address the cookie is bound to: 16 address bytes (IPv4 as ::ffff:a.b.c.d) and the port
inline void SQ_MakeAddressV4( const unsigned char ip[ 4 ], unsigned short port, uint8_t out[ SQ_ADDRESS_SIZE ] ) {
	memset( out, 0, SQ_ADDRESS_SIZE );
	out[ 10 ] = 0xff;
	out[ 11 ] = 0xff;
	memcpy( out + 12, ip, 4 );
	out[ 16 ] = static_cast< uint8_t >( port >> 8 );
	out[ 17 ] = static_cast< uint8_t >( port );
}

inline void SQ_MakeAddressV6( const unsigned char ip[ 16 ], unsigned short port, uint8_t out[ SQ_ADDRESS_SIZE ] ) {
	memcpy( out, ip, 16 );
	out[ 16 ] = static_cast< uint8_t >( port >> 8 );
	out[ 17 ] = static_cast< uint8_t >( port );
}

struct sqChallenge_t {
	int		protocol;
	int		nonce;
	int		issued;			// the server's clock, in seconds
	int		difficulty;		// leading zero bits the work must give
	uint8_t	cookie[ SQ_COOKIE_SIZE ];
};

// step 1
inline void SQ_WriteChallengeRequest( idBitMsg& msg, int protocol, int nonce ) {
	msg.WriteShort( -1 );	// CONNECTIONLESS_MESSAGE_ID
	msg.WriteString( SQ_CHALLENGE_REQUEST );
	msg.WriteLong( protocol );
	msg.WriteLong( nonce );
	for ( int i = 0; i < SQ_CHALLENGE_PAD; i++ ) {
		msg.WriteByte( 0 );
	}
}

// step 1, after its command string; a request without its padding is refused
inline bool SQ_ReadChallengeRequest( const idBitMsg& msg, int& protocol, int& nonce ) {
	if ( msg.GetRemainingData() < 8 + SQ_CHALLENGE_PAD ) {
		return false;
	}
	protocol = msg.ReadLong();
	nonce = msg.ReadLong();
	return true;
}

inline void SQ_WriteChallengeFields( idBitMsg& msg, const sqChallenge_t& challenge ) {
	msg.WriteLong( challenge.protocol );
	msg.WriteLong( challenge.nonce );
	msg.WriteLong( challenge.issued );
	msg.WriteByte( challenge.difficulty );
	for ( int i = 0; i < SQ_COOKIE_SIZE; i++ ) {
		msg.WriteByte( challenge.cookie[ i ] );
	}
}

inline bool SQ_ReadChallengeFields( const idBitMsg& msg, sqChallenge_t& challenge ) {
	if ( msg.GetRemainingData() < 13 + SQ_COOKIE_SIZE ) {
		return false;
	}
	challenge.protocol = msg.ReadLong();
	challenge.nonce = msg.ReadLong();
	challenge.issued = msg.ReadLong();
	challenge.difficulty = msg.ReadByte();
	for ( int i = 0; i < SQ_COOKIE_SIZE; i++ ) {
		challenge.cookie[ i ] = static_cast< uint8_t >( msg.ReadByte() );
	}
	return true;
}

// step 2
inline void SQ_WriteChallenge( idBitMsg& msg, const sqChallenge_t& challenge ) {
	msg.WriteShort( -1 );
	msg.WriteString( SQ_CHALLENGE );
	SQ_WriteChallengeFields( msg, challenge );
}

inline bool SQ_ReadChallenge( const idBitMsg& msg, sqChallenge_t& challenge ) {
	return SQ_ReadChallengeFields( msg, challenge ) && challenge.difficulty >= 0 && challenge.difficulty <= SQ_MAX_DIFFICULTY;
}

// step 3
inline void SQ_WriteInfoRequest( idBitMsg& msg, const sqChallenge_t& challenge, uint64_t work, bool wantProof = false ) {
	msg.WriteShort( -1 );
	msg.WriteString( SQ_REQUEST );
	SQ_WriteChallengeFields( msg, challenge );
	msg.WriteLong( static_cast< int >( work >> 32 ) );
	msg.WriteLong( static_cast< int >( work & 0xffffffffu ) );
	if ( wantProof ) {
		msg.WriteByte( 1 );
	}
}

inline bool SQ_ReadInfoRequest( const idBitMsg& msg, sqChallenge_t& challenge, uint64_t& work, bool* wantProof = NULL ) {
	if ( !SQ_ReadChallengeFields( msg, challenge ) || msg.GetRemainingData() < 8 ) {
		return false;
	}
	const uint32_t high = static_cast< uint32_t >( msg.ReadLong() );
	const uint32_t low = static_cast< uint32_t >( msg.ReadLong() );
	work = ( static_cast< uint64_t >( high ) << 32 ) | low;
	const bool proof = msg.GetRemainingData() >= 1 && msg.ReadByte() == 1;
	if ( wantProof != NULL ) {
		*wantProof = proof;
	}
	return true;
}

// the proof of work: SHA-256 of the cookie and the 8 work bytes (big-endian), one block
inline void SQ_WorkBlock( const uint8_t cookie[ SQ_COOKIE_SIZE ], uint8_t block[ sqSha256::BLOCK_SIZE ] ) {
	uint8_t message[ SQ_COOKIE_SIZE + 8 ];
	memcpy( message, cookie, SQ_COOKIE_SIZE );
	memset( message + SQ_COOKIE_SIZE, 0, 8 );
	sqSha256::PadBlock( message, sizeof( message ), block );
}

inline bool SQ_BlockMeets( uint8_t block[ sqSha256::BLOCK_SIZE ], uint64_t work, int difficulty ) {
	for ( int i = 0; i < 8; i++ ) {
		block[ SQ_COOKIE_SIZE + i ] = static_cast< uint8_t >( work >> ( 56 - 8 * i ) );
	}
	uint8_t digest[ sqSha256::DIGEST_SIZE ];
	sqSha256::HashPaddedBlock( block, digest );
	return SQ_LeadingZeroBits( digest, sqSha256::DIGEST_SIZE ) >= difficulty;
}

// does the work meet the difficulty for this cookie
inline bool SQ_WorkMeets( const uint8_t cookie[ SQ_COOKIE_SIZE ], uint64_t work, int difficulty ) {
	uint8_t block[ sqSha256::BLOCK_SIZE ];
	SQ_WorkBlock( cookie, block );
	return SQ_BlockMeets( block, work, difficulty );
}

/*
Searches for work from `work` on, at most `tries` hashes; true with `work` set when found,
else `work` is where to go on from. Clients call it in slices, on a worker thread.
*/
inline bool SQ_SolveWork( const uint8_t cookie[ SQ_COOKIE_SIZE ], int difficulty, uint64_t& work, int tries ) {
	uint8_t block[ sqSha256::BLOCK_SIZE ];
	SQ_WorkBlock( cookie, block );
	for ( int i = 0; i < tries; i++, work++ ) {
		if ( SQ_BlockMeets( block, work, difficulty ) ) {
			return true;
		}
	}
	return false;
}

/*
The server's side of the handshake: issues cookies, checks requests and sets the
difficulty. `now` is in milliseconds; the secret is drawn once at start.

Difficulty: at least `baseDifficulty`. A second with more than floodChallenges
challenge requests or floodRequests info requests raises it a bit (to SQ_MAX_DIFFICULTY);
after ten quiet seconds it comes down a bit a second. A request carries the difficulty
it was issued with (inside the HMAC, so a client cannot lower it), and is checked at
that difficulty, as long as it is not below the base.

Replay: a cookie answers once (a ring of the cookies answered within their lifetime).
*/
class sqQueryGate {
public:
	static const int SECRET_SIZE = 32;

	explicit sqQueryGate( const uint8_t secret[ SECRET_SIZE ] ) :
		baseDifficulty( 16 ), difficulty( 16 ), floodChallenges( 400 ), floodRequests( 100 ),
		windowStart( 0 ), windowChallenges( 0 ), windowRequests( 0 ), quietSince( 0 ), replayNext( 0 ) {
		memcpy( key, secret, SECRET_SIZE );
		memset( replay, 0, sizeof( replay ) );
	}

	// a new base moves the current difficulty with it, keeping any raise for a flood
	void SetBaseDifficulty( int bits ) {
		const int base = bits < SQ_MIN_DIFFICULTY ? SQ_MIN_DIFFICULTY : ( bits > SQ_MAX_DIFFICULTY ? SQ_MAX_DIFFICULTY : bits );
		if ( base == baseDifficulty ) {
			return;
		}
		const int raise = difficulty - baseDifficulty;
		baseDifficulty = base;
		difficulty = base + ( raise > 0 ? raise : 0 );
		if ( difficulty > SQ_MAX_DIFFICULTY ) {
			difficulty = SQ_MAX_DIFFICULTY;
		}
	}
	int GetDifficulty() const { return difficulty; }

	void Issue( const uint8_t address[ SQ_ADDRESS_SIZE ], int protocol, int nonce, int now, sqChallenge_t& challenge ) {
		Count( now, true );
		challenge.protocol = protocol;
		challenge.nonce = nonce;
		challenge.issued = now / 1000;
		challenge.difficulty = difficulty;
		MakeCookie( address, challenge, challenge.cookie );
	}

	// a request to answer: the cookie is ours for this address, fresh, unused, and the work is done
	bool Check( const uint8_t address[ SQ_ADDRESS_SIZE ], const sqChallenge_t& challenge, uint64_t work, int now ) {
		Count( now, false );
		const int seconds = now / 1000;
		if ( challenge.issued > seconds || seconds - challenge.issued > SQ_COOKIE_LIFETIME ) {
			return false;
		}
		if ( challenge.difficulty < baseDifficulty || challenge.difficulty > SQ_MAX_DIFFICULTY ) {
			return false;
		}
		uint8_t expected[ SQ_COOKIE_SIZE ];
		MakeCookie( address, challenge, expected );
		if ( !SQ_ConstantTimeEqual( expected, challenge.cookie, SQ_COOKIE_SIZE ) ) {
			return false;
		}
		if ( !SQ_WorkMeets( challenge.cookie, work, challenge.difficulty ) ) {
			return false;
		}
		for ( int i = 0; i < REPLAY_SLOTS; i++ ) {
			if ( replay[ i ].used && seconds - replay[ i ].issued <= SQ_COOKIE_LIFETIME &&
				memcmp( replay[ i ].cookie, challenge.cookie, SQ_COOKIE_SIZE ) == 0 ) {
				return false;
			}
		}
		replayEntry_t& slot = replay[ replayNext ];
		replayNext = ( replayNext + 1 ) % REPLAY_SLOTS;
		slot.used = true;
		slot.issued = challenge.issued;
		memcpy( slot.cookie, challenge.cookie, SQ_COOKIE_SIZE );
		return true;
	}

private:
	void MakeCookie( const uint8_t address[ SQ_ADDRESS_SIZE ], const sqChallenge_t& challenge, uint8_t cookie[ SQ_COOKIE_SIZE ] ) const {
		uint8_t message[ 1 + SQ_ADDRESS_SIZE + 13 ];
		int n = 0;
		message[ n++ ] = 1;	// cookie version
		memcpy( message + n, address, SQ_ADDRESS_SIZE );
		n += SQ_ADDRESS_SIZE;
		const uint32_t fields[ 3 ] = { static_cast< uint32_t >( challenge.protocol ), static_cast< uint32_t >( challenge.nonce ), static_cast< uint32_t >( challenge.issued ) };
		for ( int f = 0; f < 3; f++ ) {
			for ( int i = 0; i < 4; i++ ) {
				message[ n++ ] = static_cast< uint8_t >( fields[ f ] >> ( 24 - 8 * i ) );
			}
		}
		message[ n++ ] = static_cast< uint8_t >( challenge.difficulty );
		uint8_t mac[ sqSha256::DIGEST_SIZE ];
		SQ_HmacSha256( key, SECRET_SIZE, message, n, mac );
		memcpy( cookie, mac, SQ_COOKIE_SIZE );
	}

	void Count( int now, bool challenge ) {
		if ( now - windowStart >= 1000 || now < windowStart ) {
			const bool flooded = windowChallenges > floodChallenges || windowRequests > floodRequests;
			if ( flooded ) {
				difficulty = difficulty + 1 > SQ_MAX_DIFFICULTY ? SQ_MAX_DIFFICULTY : difficulty + 1;
				quietSince = now;
			} else if ( now - quietSince >= 10000 && difficulty > baseDifficulty ) {
				difficulty--;
			}
			windowStart = now;
			windowChallenges = 0;
			windowRequests = 0;
		}
		if ( challenge ) {
			windowChallenges++;
		} else {
			windowRequests++;
		}
	}

	static const int REPLAY_SLOTS = 512;
	struct replayEntry_t {
		bool	used;
		int		issued;
		uint8_t	cookie[ SQ_COOKIE_SIZE ];
	};

	uint8_t			key[ SECRET_SIZE ];
	int				baseDifficulty;
	int				difficulty;
	int				floodChallenges;
	int				floodRequests;
	int				windowStart;
	int				windowChallenges;
	int				windowRequests;
	int				quietSince;
	replayEntry_t	replay[ REPLAY_SLOTS ];
	int				replayNext;
};

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

	int playerBytes = 1 + 5;	// the count, and the state and time after the players
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
	int room = msg.GetRemainingSpace() - 1 - 5;
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
	msg.WriteByte( info.gameState & 0xff );
	msg.WriteLong( info.timeLeft );
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
	if ( msg.GetRemainingData() < 5 ) {
		return false;
	}
	info.gameState = msg.ReadByte();
	info.timeLeft = msg.ReadLong();
	if ( info.timeLeft < 0 ) {
		info.timeLeft = 0;
	}
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
