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

static void TestRequest() {
	byte buffer[ 64 ];
	idBitMsg msg;
	msg.InitWrite( buffer, sizeof( buffer ) );
	msg.BeginWriting();
	SQ_WriteRequest( msg, 0x4f450005, 12345 );
	Expect( ReadCommand( msg, SQ_REQUEST ), "request command" );
	int protocol = 0, nonce = 0;
	Expect( SQ_ReadRequest( msg, protocol, nonce ), "request reads" );
	Expect( protocol == 0x4f450005 && nonce == 12345, "request values" );

	// a request cut short is refused
	idBitMsg shortMsg;
	shortMsg.InitWrite( buffer, sizeof( buffer ) );
	shortMsg.BeginWriting();
	shortMsg.WriteShort( -1 );
	shortMsg.WriteString( SQ_REQUEST );
	shortMsg.WriteLong( 1 );
	Expect( ReadCommand( shortMsg, SQ_REQUEST ), "short request command" );
	Expect( !SQ_ReadRequest( shortMsg, protocol, nonce ), "a request without its nonce is refused" );
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
	TestRequest();
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
