// SPDX-License-Identifier: GPL-3.0-or-later
/*
===========================================================================
openETQW Asset-Free Test Harness: NetPortTest.cpp
The platform network port (src/sys/posix/posix_net.cpp): two ports on loopback exchange
a datagram over IPv4 and, where the host has IPv6, over IPv6, and each sees the other's
address and port; address text parses and prints back, IPv6 included.
===========================================================================
*/

// Compiled as the engine compiles its sources (tests/MsgChannelTest.cpp): the engine's
// precompiled.h is force-included, so idLib, idCVar and the sys declarations are here.
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/socket.h>
#include <unistd.h>

static int g_failures = 0;

static void Expect( bool ok, const char* what ) {
	if ( !ok ) {
		fprintf( stderr, "FAIL: %s\n", what );
		g_failures++;
	}
}


// idLib and the channel report through idCommon: an error or a fatal error ends
// the test with its message; Printf and Warning are printed, the rest dropped
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
	void Printf( const char *fmt, ... ) override { va_list ap; va_start( ap, fmt ); vprintf( fmt, ap ); va_end( ap ); }
	void TPrintf( const char *fmt, ... ) override {}
	void VPrintf( const char *fmt, va_list arg ) override { vprintf( fmt, arg ); }
	void DPrintf( const char *fmt, ... ) override {}
	void Warning( const char *fmt, ... ) override { va_list ap; va_start( ap, fmt ); Report( "WARNING", fmt, ap ); va_end( ap ); }
	void TWarning( const char *fmt, ... ) override {}
	void DWarning( const char *fmt, ... ) override {}
	void PrintWarnings( void ) override {}
	void ClearWarnings( const char *reason ) override {}
	void Error( const char *fmt, ... ) override { va_list ap; va_start( ap, fmt ); Report( "ERROR", fmt, ap ); va_end( ap ); exit( 1 ); }
	void FatalError( const char *fmt, ... ) override { va_list ap; va_start( ap, fmt ); Report( "FATAL ERROR", fmt, ap ); va_end( ap ); exit( 1 ); }
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

private:
	static void Report( const char *kind, const char *fmt, va_list ap ) {
		printf( "%s: ", kind );
		vprintf( fmt, ap );
		printf( "\n" );
		fflush( stdout );
	}
};


static TestCommonMock s_testCommon;
idCommon * common = &s_testCommon;
idSys * sys = NULL;
idDeclManager * declManager = NULL;
idCVar * idCVar::staticVars = NULL;
idCVarSystem * cvarSystem = NULL;
idFileSystem * fileSystem = NULL;

// cvars read 0 until a cvar system registers them (none here): set the ones the code reads
struct TestCVar : public idCVar {
	static void Set( idCVar& cvar, int value ) {
		TestCVar& poked = static_cast< TestCVar& >( cvar );
		poked.integerValue = value;
		poked.floatValue = static_cast< float >( value );
	}
};
extern idCVar net_ipv6;

int Sys_Milliseconds( void ) {
	struct timespec now;
	clock_gettime( CLOCK_MONOTONIC, &now );
	return static_cast< int >( now.tv_sec * 1000 + now.tv_nsec / 1000000 );
}

// a datagram from one port to the other, waited for a second at most
static bool Exchange( idPort& from, idPort& to, const netadr_t& toAddress, netadr_t& seenFrom ) {
	const char message[] = "openETQW";
	from.SendPacket( toAddress, message, sizeof( message ) );
	char buffer[ 64 ];
	int size = 0;
	for ( int i = 0; i < 100; i++ ) {
		if ( to.GetPacket( seenFrom, buffer, size, sizeof( buffer ) ) ) {
			return size == static_cast< int >( sizeof( message ) ) && memcmp( buffer, message, size ) == 0;
		}
		usleep( 10000 );
	}
	return false;
}

static bool HostHasIPv6() {
	const int s = socket( AF_INET6, SOCK_DGRAM, 0 );
	if ( s < 0 ) {
		return false;
	}
	close( s );
	return true;
}

int main() {
	TestCVar::Set( net_ipv6, 1 );
	idPort server, client;
	Expect( server.InitForPort( PORT_ANY ), "a server port" );
	Expect( client.InitForPort( PORT_ANY ), "a client port" );

	netadr_t to;
	Expect( Sys_StringToNetAdr( va( "127.0.0.1:%d", server.GetPort() ), &to, false ), "parse 127.0.0.1" );
	to.type = NA_IP;	// "127.0.0.1" parses as loopback; send to it as an address
	to.ip[ 0 ] = 127; to.ip[ 1 ] = 0; to.ip[ 2 ] = 0; to.ip[ 3 ] = 1;
	netadr_t seen;
	Expect( Exchange( client, server, to, seen ), "IPv4 datagram" );
	Expect( seen.type == NA_IP && seen.ip[ 0 ] == 127 && seen.port == client.GetPort(), "IPv4 sender address" );

	netadr_t parsed;
	Expect( Sys_StringToNetAdr( "[2001:db8::7]:27733", &parsed, false ) && parsed.type == NA_IP6 && parsed.port == 27733, "parse [v6]:port" );
	Expect( strcmp( Sys_NetAdrToString( parsed ), "[2001:db8::7]:27733" ) == 0, "print [v6]:port" );
	Expect( ( parsed.ip[ 0 ] & 0xf0 ) == 0xf0, "an IPv6 address carries its key in ip" );
	Expect( Sys_StringToNetAdr( "::ffff:198.51.100.7:27733", &parsed, false ) == false || parsed.type != NA_IP6, "a bare v6 has no port" );
	Expect( Sys_StringToNetAdr( "[::ffff:198.51.100.7]:5", &parsed, false ) && parsed.type == NA_IP && parsed.ip[ 0 ] == 198 && parsed.port == 5, "v4-mapped becomes IPv4" );
	Expect( !Sys_StringToNetAdr( "[2001:db8::7", &parsed, false ), "refuses an unclosed bracket" );
	Expect( Sys_StringToNetAdr( "fd00::1", &parsed, false ) && Sys_IsLANAddress( parsed ), "unique local is LAN" );
	Expect( Sys_StringToNetAdr( "2001:db8::1", &parsed, false ) && !Sys_IsLANAddress( parsed ), "global is not LAN" );
	netadr_t other;
	Sys_StringToNetAdr( "[2001:db8::1]:1", &other, false );
	Expect( Sys_CompareNetAdrBase( parsed, other ), "same IPv6 base, other port" );
	Sys_StringToNetAdr( "[2001:db8::2]:1", &other, false );
	Expect( !Sys_CompareNetAdrBase( parsed, other ), "other IPv6 base" );

	if ( HostHasIPv6() ) {
		netadr_t to6;
		Expect( Sys_StringToNetAdr( va( "[::1]:%d", server.GetPort() ), &to6, false ) && to6.type == NA_IP6, "parse [::1]" );
		Expect( Exchange( client, server, to6, seen ), "IPv6 datagram on the IPv4 port's number" );
		Expect( seen.type == NA_IP6 && strncmp( Sys_NetAdrToString( seen ), "[::1]:", 6 ) == 0, "IPv6 sender address" );
		// and back, to the address the server saw
		netadr_t back;
		Expect( Exchange( server, client, seen, back ), "IPv6 reply" );
	} else {
		printf( "NetPortTest: no IPv6 on this host; IPv4 only\n" );
	}

	server.Close();
	client.Close();
	if ( g_failures == 0 ) {
		printf( "NetPortTest: all checks passed\n" );
	}
	return g_failures == 0 ? 0 : 1;
}
