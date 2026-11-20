// SPDX-License-Identifier: GPL-3.0-or-later
/*
===========================================================================
openETQW Asset-Free Test Harness: ServerQueryTest.cpp
The server browser's query (src/framework/async/ServerQuery.h): a request and a response
read back as written; a response sized for one packet keeps the shown keys and the
players and leaves out what does not fit; malformed and hostile responses are refused;
and the server's rate limiter holds an address to one answer per interval and everyone
to the per-second cap.
===========================================================================
*/

#include "src/idlib/precompiled.h"
#include "src/idlib/BitMsg.h"
#include "src/framework/Common_public.h"
#include "src/framework/async/ServerQuery.h"

#include <cstdio>
#include <cstring>

static int g_failures = 0;

static void Expect( bool ok, const char* what ) {
	if ( !ok ) {
		fprintf( stderr, "FAIL: %s\n", what );
		g_failures++;
	}
}

// Minimal mock idCommon to intercept Printf and prevent null dereference on overflow
class TestCommonMock : public idCommon {
public:
    void Init( int argc, const char **argv, const char *cmdline ) override {}
    void Shutdown( void ) override {}
    void Quit( void ) override {}
    bool IsInitialized( void ) const override { return true; }
    void Frame( void ) override {}
    void Async( void ) override {}
    bool StartupVariable( const char *match ) override { return false; }
    void ClearStartupVariable( const char* match ) override {}
    void WriteConfigToFile( const char *filename, bool writeBindings = true, bool writeCVars = true ) override {}
    void WriteFlaggedCVarsToFile( const char *filename, int flags, const char *setCmd ) override {}
    void BeginRedirect( char *buffer, int buffersize, void *user, void (*flush)( void *, const char * ) ) override {}
    void EndRedirect( void ) override {}
    void SetRefreshOnPrint( bool set ) override {}
    void Printf( const char *fmt, ... ) override {}
    void TPrintf( const char *fmt, ... ) override {}
    void VPrintf( const char *fmt, va_list arg ) override {}
    void DPrintf( const char *fmt, ... ) override {}
    void Warning( const char *fmt, ... ) override {}
    void TWarning( const char *fmt, ... ) override {}
    void DWarning( const char *fmt, ... ) override {}
    void PrintWarnings( void ) override {}
    void ClearWarnings( const char *reason ) override {}
    void Error( const char *fmt, ... ) override {}
    void FatalError( const char *fmt, ... ) override {}
    void PrintLoadingMessage( const char *msg ) override {}
    void EnableWarnings( void ) override {}
    void DisableWarnings( void ) override {}
    void PacifierUpdate( void ) override {}
    void UpdateLevelLoadScreen( const wchar_t* status ) override {}
    const idLangDict* GetLanguageDict( void ) override { return NULL; }
    idWStr LocalizeText( const char* declName, const idWStrList& arguments = idWStrList() ) override { return idWStr(); }
    idWStr LocalizeText( const sdDeclLocStr* loc, const idWStrList& arguments = idWStrList() ) override { return idWStr(); }
    int GetNumVideoModes( void ) const override { return 0; }
    vidmode_t& GetVideoMode( int index ) const override { static vidmode_t vm; return vm; }
    idSoundWorld* GetGameSoundWorld( void ) override { return NULL; }
    idSoundWorld* GetMenuSoundWorld( void ) override { return NULL; }
    void WriteConfigs( void ) override {}
};

static TestCommonMock s_testCommon;
idCommon * common = &s_testCommon;
idSys * sys = NULL;

// skips the connectionless header and the command, as the receivers do
static bool ReadCommand( idBitMsg& msg, const char* expected ) {
	msg.BeginReading();
	if ( msg.ReadShort() != -1 ) {
		return false;
	}
	char command[ 32 ];
	msg.ReadString( command, sizeof( command ) );
	return strcmp( command, expected ) == 0;
}

static void Hex( const uint8_t* bytes, int size, char* out ) {
	for ( int i = 0; i < size; i++ ) {
		snprintf( out + i * 2, 3, "%02x", bytes[ i ] );
	}
}

static void TestHashes() {
	uint8_t digest[ 32 ];
	char hex[ 65 ];
	sqSha256::Hash( "abc", 3, digest );
	Hex( digest, 32, hex );
	Expect( strcmp( hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ) == 0, "SHA-256 of abc" );
	sqSha256::Hash( "", 0, digest );
	Hex( digest, 32, hex );
	Expect( strcmp( hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" ) == 0, "SHA-256 of nothing" );
	const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	sqSha256::Hash( two, strlen( two ), digest );
	Hex( digest, 32, hex );
	Expect( strcmp( hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" ) == 0, "SHA-256 over two blocks" );

	// RFC 4231 test cases 1 and 2
	uint8_t key1[ 20 ];
	memset( key1, 0x0b, sizeof( key1 ) );
	SQ_HmacSha256( key1, sizeof( key1 ), "Hi There", 8, digest );
	Hex( digest, 32, hex );
	Expect( strcmp( hex, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7" ) == 0, "HMAC-SHA-256 RFC 4231 case 1" );
	SQ_HmacSha256( "Jefe", 4, "what do ya want for nothing?", 28, digest );
	Hex( digest, 32, hex );
	Expect( strcmp( hex, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843" ) == 0, "HMAC-SHA-256 RFC 4231 case 2" );

	// the one-block fast path agrees with the general one
	uint8_t message[ 24 ] = { 1, 2, 3, 4, 5 };
	uint8_t block[ 64 ];
	uint8_t slow[ 32 ];
	sqSha256::PadBlock( message, sizeof( message ), block );
	sqSha256::HashPaddedBlock( block, digest );
	sqSha256::Hash( message, sizeof( message ), slow );
	Expect( memcmp( digest, slow, 32 ) == 0, "the padded one-block hash" );

	const uint8_t zeros[ 4 ] = { 0x00, 0x00, 0x1f, 0xff };
	Expect( SQ_LeadingZeroBits( zeros, 4 ) == 19, "leading zero bits" );
}

static void TestHandshake() {
	byte buffer[ 128 ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	SQ_WriteChallengeRequest( msg, 0x4f450005, 12345 );
	const int requestSize = msg.GetSize();
	Expect( ReadCommand( msg, SQ_CHALLENGE_REQUEST ), "challenge request command" );
	int protocol = 0, nonce = 0;
	Expect( SQ_ReadChallengeRequest( msg, protocol, nonce ) && protocol == 0x4f450005 && nonce == 12345, "challenge request reads" );

	// without its padding the request is refused
	msg.BeginWriting();
	msg.WriteShort( -1 );
	msg.WriteString( SQ_CHALLENGE_REQUEST );
	msg.WriteLong( 1 );
	msg.WriteLong( 2 );
	Expect( ReadCommand( msg, SQ_CHALLENGE_REQUEST ) && !SQ_ReadChallengeRequest( msg, protocol, nonce ), "an unpadded challenge request is refused" );

	uint8_t secret[ sqQueryGate::SECRET_SIZE ];
	for ( int i = 0; i < sqQueryGate::SECRET_SIZE; i++ ) {
		secret[ i ] = static_cast< uint8_t >( i * 7 + 1 );
	}
	sqQueryGate gate( secret );
	gate.SetBaseDifficulty( 10 );	// cheap for the test
	const unsigned char ip[ 4 ] = { 192, 0, 2, 7 };
	const unsigned char otherIp[ 4 ] = { 198, 51, 100, 9 };
	uint8_t address[ SQ_ADDRESS_SIZE ];
	uint8_t other[ SQ_ADDRESS_SIZE ];
	SQ_MakeAddressV4( ip, 27960, address );
	SQ_MakeAddressV4( otherIp, 27960, other );

	sqChallenge_t challenge;
	const int now = 100000;
	gate.Issue( address, 0x4f450005, 12345, now, challenge );
	Expect( challenge.difficulty == 10, "issued at the base difficulty" );

	// the challenge is no bigger than the request: no amplification
	msg.BeginWriting();
	SQ_WriteChallenge( msg, challenge );
	Expect( msg.GetSize() <= requestSize, "the challenge answer is not bigger than its request" );
	sqChallenge_t read;
	Expect( ReadCommand( msg, SQ_CHALLENGE ) && SQ_ReadChallenge( msg, read ) && memcmp( read.cookie, challenge.cookie, SQ_COOKIE_SIZE ) == 0 && read.nonce == 12345, "the challenge reads" );

	uint64_t work = 0;
	Expect( SQ_SolveWork( challenge.cookie, challenge.difficulty, work, 1 << 20 ), "the work is found" );
	Expect( SQ_WorkMeets( challenge.cookie, work, challenge.difficulty ), "the found work meets the difficulty" );

	msg.BeginWriting();
	SQ_WriteInfoRequest( msg, challenge, work );
	sqChallenge_t request;
	uint64_t requestWork = 0;
	Expect( ReadCommand( msg, SQ_REQUEST ) && SQ_ReadInfoRequest( msg, request, requestWork ) && requestWork == work, "the info request reads" );

	Expect( !gate.Check( other, request, requestWork, now + 500 ), "a cookie for another address is refused" );
	sqChallenge_t lowered = request;
	lowered.difficulty = 9;
	Expect( !gate.Check( address, lowered, requestWork, now + 500 ), "a lowered difficulty is refused" );
	Expect( !gate.Check( address, request, requestWork + 1, now + 500 ) || SQ_WorkMeets( request.cookie, requestWork + 1, 10 ), "wrong work is refused" );
	Expect( !gate.Check( address, request, requestWork, now + ( SQ_COOKIE_LIFETIME + 2 ) * 1000 ), "an old cookie is refused" );
	Expect( gate.Check( address, request, requestWork, now + 500 ), "the right request is answered" );
	Expect( !gate.Check( address, request, requestWork, now + 600 ), "a cookie answers once" );

	// a flood raises the difficulty, quiet brings it back down
	sqQueryGate flooded( secret );
	flooded.SetBaseDifficulty( 16 );
	int t = 0;
	for ( int second = 0; second < 5; second++ ) {
		for ( int i = 0; i < 600; i++ ) {
			flooded.Issue( address, 1, i, t + i, challenge );
		}
		t += 1000;
	}
	flooded.Issue( address, 1, 0, t, challenge );
	Expect( flooded.GetDifficulty() > 16, "a flood raises the difficulty" );
	const int raised = flooded.GetDifficulty();
	for ( int second = 0; second < 40; second++ ) {
		t += 1000;
		flooded.Issue( address, 1, 0, t, challenge );
	}
	Expect( flooded.GetDifficulty() < raised && flooded.GetDifficulty() >= 16, "quiet brings it back down, not below the base" );
}

static void TestRoundTrip() {
	static sqInfo_t info;
	info.Clear();
	info.protocol = 7;
	info.nonce = 99;
	info.AddKey( "si_name", "My ETQW Server" );
	info.AddKey( "si_map", "maps/valley" );
	info.AddKey( "si_maxPlayers", "24" );
	info.AddPlayer( "Alice", 42, false );
	info.AddPlayer( "Bot 3", 0, true );

	byte buffer[ 1400 ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	SQ_WriteResponse( msg, info );
	Expect( msg.GetSize() < 200, "a small response stays small" );

	static sqInfo_t read;
	Expect( ReadCommand( msg, SQ_RESPONSE ), "response command" );
	Expect( SQ_ReadResponse( msg, read ), "response reads" );
	Expect( read.protocol == 7 && read.nonce == 99, "response header" );
	Expect( read.numKeys == 3 && read.FindValue( "si_map" ) != NULL && strcmp( read.FindValue( "si_map" ), "maps/valley" ) == 0, "keys" );
	Expect( read.numPlayers == 2 && strcmp( read.players[ 0 ].name, "Alice" ) == 0 && read.players[ 0 ].ping == 42 && !read.players[ 0 ].bot, "a player" );
	Expect( read.players[ 1 ].bot, "a bot" );
}

static void TestOnePacket() {
	// more keys and players than one packet holds: the players and the first keys stay
	static sqInfo_t info;
	info.Clear();
	info.AddKey( "si_name", "first" );
	char key[ 32 ];
	char value[ SQ_MAX_VALUE_LENGTH ];
	memset( value, 'v', sizeof( value ) - 1 );
	value[ sizeof( value ) - 1 ] = '\0';
	for ( int i = 1; i < SQ_MAX_KEYS; i++ ) {
		snprintf( key, sizeof( key ), "key%d", i );
		info.AddKey( key, value );
	}
	Expect( !info.AddKey( "one_too_many", "x" ), "the key table is full at SQ_MAX_KEYS" );
	for ( int i = 0; i < 32; i++ ) {
		snprintf( key, sizeof( key ), "Player%02d", i );
		info.AddPlayer( key, i, false );
	}

	byte buffer[ 1400 ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	SQ_WriteResponse( msg, info );
	Expect( msg.GetSize() <= 1400 && !msg.IsOverflowed(), "fits one packet" );

	static sqInfo_t read;
	Expect( ReadCommand( msg, SQ_RESPONSE ) && SQ_ReadResponse( msg, read ), "the packed response reads" );
	Expect( read.numPlayers == 32, "every player kept" );
	Expect( read.numKeys >= 1 && read.numKeys < SQ_MAX_KEYS && strcmp( read.keys[ 0 ], "si_name" ) == 0, "the first keys kept, the rest left out" );
}

static void TestHostile() {
	byte buffer[ 256 ];
	static sqInfo_t read;

	// a key count above the limit
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	msg.WriteShort( -1 );
	msg.WriteString( SQ_RESPONSE );
	msg.WriteLong( 1 );
	msg.WriteLong( 2 );
	msg.WriteShort( 30000 );
	Expect( ReadCommand( msg, SQ_RESPONSE ) && !SQ_ReadResponse( msg, read ), "a huge key count is refused" );

	// a negative key count
	msg.BeginWriting();
	msg.WriteShort( -1 );
	msg.WriteString( SQ_RESPONSE );
	msg.WriteLong( 1 );
	msg.WriteLong( 2 );
	msg.WriteShort( -5 );
	Expect( ReadCommand( msg, SQ_RESPONSE ) && !SQ_ReadResponse( msg, read ), "a negative key count is refused" );

	// keys promised but the message ends
	msg.BeginWriting();
	msg.WriteShort( -1 );
	msg.WriteString( SQ_RESPONSE );
	msg.WriteLong( 1 );
	msg.WriteLong( 2 );
	msg.WriteShort( 10 );
	msg.WriteString( "a" );
	msg.WriteString( "b" );
	Expect( ReadCommand( msg, SQ_RESPONSE ) && !SQ_ReadResponse( msg, read ), "a truncated key list is refused" );

	// a player count above the limit
	msg.BeginWriting();
	msg.WriteShort( -1 );
	msg.WriteString( SQ_RESPONSE );
	msg.WriteLong( 1 );
	msg.WriteLong( 2 );
	msg.WriteShort( 0 );
	msg.WriteByte( 200 );
	Expect( ReadCommand( msg, SQ_RESPONSE ) && !SQ_ReadResponse( msg, read ), "a huge player count is refused" );

	// an over-long, unterminated name is cut to the limit, not overrun
	byte big[ 1024 ];
	idBitMsg longMsg;
	longMsg.InitWrite( big, sizeof( big ) );
	longMsg.BeginWriting();
	longMsg.WriteShort( -1 );
	longMsg.WriteString( SQ_RESPONSE );
	longMsg.WriteLong( 1 );
	longMsg.WriteLong( 2 );
	longMsg.WriteShort( 1 );
	char longKey[ 600 ];
	memset( longKey, 'k', sizeof( longKey ) - 1 );
	longKey[ sizeof( longKey ) - 1 ] = '\0';
	longMsg.WriteString( longKey, -1, false );
	longMsg.WriteString( "v" );
	longMsg.WriteByte( 0 );
	Expect( ReadCommand( longMsg, SQ_RESPONSE ) && SQ_ReadResponse( longMsg, read ), "a long key reads" );
	Expect( strlen( read.keys[ 0 ] ) == SQ_MAX_KEY_LENGTH - 1 && strcmp( read.values[ 0 ], "v" ) == 0, "a long key is cut and the next field still reads" );
}

static void TestRateLimiter() {
	sqRateLimiter limiter( 100, 5 );
	Expect( limiter.Allow( 1, 1000 ), "first query answered" );
	Expect( !limiter.Allow( 1, 1050 ), "the same address within the interval is not" );
	Expect( limiter.Allow( 1, 1100 ), "the same address after the interval is" );
	Expect( limiter.Allow( 2, 1100 ) && limiter.Allow( 3, 1100 ) && limiter.Allow( 4, 1100 ), "other addresses" );
	Expect( !limiter.Allow( 5, 1100 ), "the per-second cap (5) holds" );
	Expect( limiter.Allow( 5, 2100 ), "a new second" );

	// many addresses (a spoofed flood) never pass more than the cap a second
	sqRateLimiter flood( 100, 60 );
	int answered = 0;
	for ( unsigned int i = 0; i < 10000; i++ ) {
		if ( flood.Allow( i, 5000 + static_cast< int >( i / 100 ) ) ) {	// 10000 queries in 100 ms
			answered++;
		}
	}
	Expect( answered <= 60, "a flood gets at most the cap" );
}

int main() {
	TestHashes();
	TestHandshake();
	TestRoundTrip();
	TestOnePacket();
	TestHostile();
	TestRateLimiter();
	if ( g_failures != 0 ) {
		printf( "%d failure(s)\n", g_failures );
		return 1;
	}
	printf( "server query: all checks passed\n" );
	return 0;
}
