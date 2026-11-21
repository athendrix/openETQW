// SPDX-License-Identifier: GPL-3.0-or-later
/*
===========================================================================
openETQW Asset-Free Test Harness: NetAddressTest.cpp
IPv6 address text (src/sys/NetAddress.h): parsing RFC 4291 forms and refusing malformed
ones, RFC 5952 formatting, host:port splitting with brackets, LAN and global
classification, and the 32-bit key of an address's /64.
===========================================================================
*/

#include "src/sys/NetAddress.h"

#include <cstdio>

static int g_failures = 0;

static void Expect( bool ok, const char* what ) {
	if ( !ok ) {
		fprintf( stderr, "FAIL: %s\n", what );
		g_failures++;
	}
}

static bool RoundTrip( const char* in, const char* expected ) {
	uint8_t ip[ 16 ];
	if ( !NA_ParseIPv6( in, ip ) ) {
		fprintf( stderr, "  '%s' did not parse\n", in );
		return false;
	}
	char out[ 64 ];
	NA_FormatIPv6( ip, out, sizeof( out ) );
	if ( strcmp( out, expected ) != 0 ) {
		fprintf( stderr, "  '%s' -> '%s', expected '%s'\n", in, out, expected );
		return false;
	}
	return true;
}

int main() {
	Expect( RoundTrip( "::", "::" ), "all zeros" );
	Expect( RoundTrip( "::1", "::1" ), "loopback" );
	Expect( RoundTrip( "2001:db8::1", "2001:db8::1" ), "compressed middle" );
	Expect( RoundTrip( "2001:0DB8:0000:0000:0000:0000:0000:0001", "2001:db8::1" ), "full form, upper case" );
	Expect( RoundTrip( "2001:db8:0:1:1:1:1:1", "2001:db8:0:1:1:1:1:1" ), "one zero group is not compressed" );
	Expect( RoundTrip( "2001:db8:0:0:1:0:0:1", "2001:db8::1:0:0:1" ), "the first of equal runs" );
	Expect( RoundTrip( "2001:0:0:1:0:0:0:1", "2001:0:0:1::1" ), "the longest run" );
	Expect( RoundTrip( "fe80::", "fe80::" ), "trailing zeros" );
	Expect( RoundTrip( "::ffff:198.51.100.7", "::ffff:c633:6407" ), "IPv4 tail" );
	Expect( RoundTrip( "1:2:3:4:5:6:7:8", "1:2:3:4:5:6:7:8" ), "no zeros" );

	const char* bad[] = { "", ":", ":::", "1:::2", "1::2::3", "1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7", "12345::", "g::1",
		"1:", ":1", "::1.2.3", "::1.2.3.256", "::1.2.3.4.5", "1.2.3.4::", "1:2:3:4:5:6:7:1.2.3.4", "::01234" };
	for ( const char* text : bad ) {
		uint8_t ip[ 16 ];
		char what[ 64 ];
		snprintf( what, sizeof( what ), "refuses '%s'", text );
		Expect( !NA_ParseIPv6( text, ip ), what );
	}
	uint8_t ip[ 16 ];
	Expect( !NA_ParseIPv6( NULL, ip ), "refuses NULL" );

	char host[ 64 ];
	int port = -1;
	Expect( NA_SplitHostPort( "[2001:db8::1]:27733", host, sizeof( host ), &port ) && strcmp( host, "2001:db8::1" ) == 0 && port == 27733, "[v6]:port" );
	Expect( NA_SplitHostPort( "[2001:db8::1]", host, sizeof( host ), &port ) && strcmp( host, "2001:db8::1" ) == 0 && port == 0, "[v6]" );
	Expect( NA_SplitHostPort( "2001:db8::1", host, sizeof( host ), &port ) && strcmp( host, "2001:db8::1" ) == 0 && port == 0, "bare v6 has no port" );
	Expect( NA_SplitHostPort( "198.51.100.7:27733", host, sizeof( host ), &port ) && strcmp( host, "198.51.100.7" ) == 0 && port == 27733, "v4:port" );
	Expect( NA_SplitHostPort( "example.org", host, sizeof( host ), &port ) && strcmp( host, "example.org" ) == 0 && port == 0, "a name" );
	Expect( NA_SplitHostPort( "fe80::1%eth0", host, sizeof( host ), &port ) && strcmp( host, "fe80::1%eth0" ) == 0, "a zone stays in the host" );
	Expect( !NA_SplitHostPort( "[2001:db8::1", host, sizeof( host ), &port ), "unclosed bracket" );
	Expect( !NA_SplitHostPort( "[2001:db8::1]x", host, sizeof( host ), &port ), "junk after the bracket" );
	Expect( !NA_SplitHostPort( "host:99999", host, sizeof( host ), &port ), "port out of range" );
	Expect( !NA_SplitHostPort( "host:12a", host, sizeof( host ), &port ), "port not a number" );
	Expect( !NA_SplitHostPort( "host:", host, sizeof( host ), &port ), "empty port" );
	Expect( !NA_SplitHostPort( ":27733", host, sizeof( host ), &port ), "empty host" );
	char tiny[ 4 ];
	Expect( !NA_SplitHostPort( "example.org", tiny, sizeof( tiny ), &port ), "host too long for the buffer" );

	NA_ParseIPv6( "fe80::1", ip );
	Expect( NA_IsLinkLocalIPv6( ip ) && NA_IsLANIPv6( ip ) && !NA_IsGlobalIPv6( ip ), "link-local" );
	NA_ParseIPv6( "fd12:3456::1", ip );
	Expect( NA_IsLANIPv6( ip ) && !NA_IsGlobalIPv6( ip ), "unique local" );
	NA_ParseIPv6( "2001:db8::1", ip );
	Expect( !NA_IsLANIPv6( ip ) && NA_IsGlobalIPv6( ip ), "global" );
	NA_ParseIPv6( "::1", ip );
	Expect( NA_IsLoopbackIPv6( ip ) && NA_IsLANIPv6( ip ), "loopback is local" );
	NA_ParseIPv6( "::ffff:198.51.100.7", ip );
	Expect( NA_IsV4Mapped( ip ) && !NA_IsGlobalIPv6( ip ), "v4-mapped" );

	uint8_t a[ 16 ], b[ 16 ], c[ 16 ];
	NA_ParseIPv6( "2001:db8:1:2::1", a );
	NA_ParseIPv6( "2001:db8:1:2:ffff::99", b );
	NA_ParseIPv6( "2001:db8:1:3::1", c );
	uint8_t ka[ 4 ], kb[ 4 ], kc[ 4 ];
	NA_Key32( a, ka );
	NA_Key32( b, kb );
	NA_Key32( c, kc );
	Expect( memcmp( ka, kb, 4 ) == 0, "one /64, one key" );
	Expect( memcmp( ka, kc, 4 ) != 0, "another /64, another key" );
	Expect( ( ka[ 0 ] & 0xf0 ) == 0xf0 && ( kc[ 0 ] & 0xf0 ) == 0xf0, "keys are in 240.0.0.0/4" );

	if ( g_failures == 0 ) {
		printf( "NetAddressTest: all checks passed\n" );
	}
	return g_failures == 0 ? 0 : 1;
}
