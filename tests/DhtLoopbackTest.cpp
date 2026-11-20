// SPDX-License-Identifier: GPL-3.0-or-later
/*
===========================================================================
openETQW Asset-Free Test Harness: DhtLoopbackTest.cpp
The engine's DHT node (src/sdnet/Dht.cpp over jech/dht and posix_net.cpp) between two
processes on this host: node A announces a game port, node B, which A told about
itself, finds it. jech/dht keeps one node per process, hence the fork; it ignores
127.0.0.0/8, so the nodes talk over the host's first non-loopback IPv4 address (the
test is skipped without one). Nothing leaves the host.
===========================================================================
*/

// Compiled as the engine compiles its sources (tests/MsgChannelTest.cpp)
#include "sdnet/Dht.h"
#include "sys/LocalSubnets.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

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

int Sys_Milliseconds( void ) {
	struct timespec now;
	clock_gettime( CLOCK_MONOTONIC, &now );
	return static_cast< int >( now.tv_sec * 1000 + now.tv_nsec / 1000000 );
}

idCmdSystem * cmdSystem = NULL;
extern "C" FILE* dht_debug;	// jech/dht's log: DHT_TEST_DEBUG=1
extern idCVar net_dhtReadyNodes;
extern idCVar net_dht;
extern idCVar net_dhtRouters;
extern idCVar net_ipv6;

// cvars read 0 until a cvar system registers them (none here): set the ones the code reads
struct TestCVar : public idCVar {
	static void Set( idCVar& cvar, int value ) {
		TestCVar& poked = static_cast< TestCVar& >( cvar );
		poked.integerValue = value;
		poked.floatValue = static_cast< float >( value );
	}
};


static const int ANNOUNCED_PORT = 27911;

// reads and hands the DHT its packets for a while
static void Run( idPort& port, int milliseconds, bool ( *done )() ) {
	const int end = Sys_Milliseconds() + milliseconds;
	byte buffer[ MAX_MESSAGE_SIZE ];
	while ( Sys_Milliseconds() < end && ( done == NULL || !done() ) ) {
		netadr_t from;
		int size = 0;
		while ( port.GetPacket( from, buffer, size, sizeof( buffer ) ) ) {
			if ( DHT_IsPacket( buffer, size ) ) {
				DHT_Input( from, buffer, size );
			}
		}
		DHT_Frame();
		usleep( 10000 );
	}
}

static bool found = false;
static netadr_t hostAddress;

static void Peer( const netadr_t& peer ) {
	if ( peer.type == NA_IP && memcmp( peer.ip, hostAddress.ip, 4 ) == 0 && peer.port == ANNOUNCED_PORT ) {
		found = true;
	}
}

static bool Found() {
	return found;
}

int main() {
	lanSubnet_t subnets[ 4 ];
	if ( Sys_GetLocalSubnets( subnets, 4 ) == 0 ) {
		printf( "DhtLoopbackTest: no non-loopback IPv4 address; skipped\n" );
		return 77;	// meson's "skipped"
	}
	memset( &hostAddress, 0, sizeof( hostAddress ) );
	hostAddress.type = NA_IP;
	hostAddress.ip[ 0 ] = static_cast< byte >( subnets[ 0 ].address >> 24 );
	hostAddress.ip[ 1 ] = static_cast< byte >( subnets[ 0 ].address >> 16 );
	hostAddress.ip[ 2 ] = static_cast< byte >( subnets[ 0 ].address >> 8 );
	hostAddress.ip[ 3 ] = static_cast< byte >( subnets[ 0 ].address );
	if ( getenv( "DHT_TEST_DEBUG" ) != NULL ) {
		dht_debug = stderr;
	}
	TestCVar::Set( net_dht, 1 );
	TestCVar::Set( net_dhtRouters, 0 );	// a DHT of two: never the public one
	TestCVar::Set( net_ipv6, 0 );	// IPv4 between the nodes
	TestCVar::Set( net_dhtReadyNodes, 1 );

	idPort portB;
	if ( !portB.InitForPort( PORT_ANY ) ) {
		printf( "FAIL: no port\n" );
		return 1;
	}
	netadr_t b = hostAddress;
	b.port = static_cast< unsigned short >( portB.GetPort() );
	int fromChild[ 2 ];
	if ( pipe( fromChild ) != 0 ) {
		return 1;
	}
	const pid_t child = fork();
	if ( child == 0 ) {
		// node A: knows B, announces ANNOUNCED_PORT, and answers until it is stopped
		portB.Close();
		idPort portA;
		if ( !portA.InitForPort( PORT_ANY ) || !DHT_Start( &portA ) ) {
			_exit( 2 );
		}
		const unsigned short portNumber = static_cast< unsigned short >( portA.GetPort() );
		if ( write( fromChild[ 1 ], &portNumber, sizeof( portNumber ) ) != static_cast< ssize_t >( sizeof( portNumber ) ) ) {
			_exit( 2 );
		}
		DHT_AddNode( b );
		DHT_SetAnnouncePort( ANNOUNCED_PORT );
		Run( portA, 45000, NULL );
		_exit( 0 );
	}

	// node B: knows A, then looks for servers once A has had time to announce
	close( fromChild[ 1 ] );	// a child that dies before writing ends the read
	unsigned short portNumber = 0;
	const bool started = read( fromChild[ 0 ], &portNumber, sizeof( portNumber ) ) == static_cast< ssize_t >( sizeof( portNumber ) );
	bool ok = started && DHT_Start( &portB );
	if ( ok ) {
		netadr_t a = hostAddress;
		a.port = portNumber;
		DHT_SetPeerFunc( Peer );
		DHT_AddNode( a );
		// A's announce follows its own search of the DHT, which waits out its retries
		Run( portB, 20000, NULL );
		DHT_Search();
		Run( portB, 15000, Found );
	}
	kill( child, SIGTERM );
	int status = 0;
	waitpid( child, &status, 0 );
	if ( !started ) {
		printf( "node A exited with %d\n", WIFEXITED( status ) ? WEXITSTATUS( status ) : -1 );
	}
	DHT_Stop( &portB );
	if ( !started || !ok ) {
		printf( "FAIL: a DHT node did not start\n" );
		return 1;
	}
	if ( !found ) {
		printf( "FAIL: node B did not find the port node A announced\n" );
		return 1;
	}
	printf( "DhtLoopbackTest: node B found node A's announced server\n" );
	return 0;
}
