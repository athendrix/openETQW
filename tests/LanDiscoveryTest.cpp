// SPDX-License-Identifier: GPL-3.0-or-later
/*
===========================================================================
openETQW Asset-Free Test Harness: LanDiscoveryTest.cpp
The LAN search's subnet arithmetic (src/sdnet/LanDiscovery.h): directed broadcast
addresses, and the hosts the slow sweep asks, in order, within its cap, never our own
address or a subnet's network or broadcast address, and never a public subnet.
===========================================================================
*/

#include "src/sdnet/LanDiscovery.h"

#include <cstdio>
#include <set>

static int g_failures = 0;

static void Expect( bool ok, const char* what ) {
	if ( !ok ) {
		fprintf( stderr, "FAIL: %s\n", what );
		g_failures++;
	}
}

static uint32_t Ip( int a, int b, int c, int d ) {
	return ( static_cast< uint32_t >( a ) << 24 ) | ( static_cast< uint32_t >( b ) << 16 ) | ( static_cast< uint32_t >( c ) << 8 ) | static_cast< uint32_t >( d );
}

static bool Has( const std::vector< uint32_t >& list, uint32_t address ) {
	for ( uint32_t a : list ) {
		if ( a == address ) {
			return true;
		}
	}
	return false;
}

static bool Unique( const std::vector< uint32_t >& list ) {
	return std::set< uint32_t >( list.begin(), list.end() ).size() == list.size();
}

int main() {
	// directed broadcasts
	Expect( LAN_DirectedBroadcast( { Ip( 192, 168, 1, 20 ), Ip( 255, 255, 255, 0 ) } ) == Ip( 192, 168, 1, 255 ), "a /24's broadcast" );
	Expect( LAN_DirectedBroadcast( { Ip( 10, 1, 2, 3 ), Ip( 255, 0, 0, 0 ) } ) == Ip( 10, 255, 255, 255 ), "a /8's broadcast" );
	Expect( LAN_DirectedBroadcast( { Ip( 10, 0, 0, 1 ), Ip( 255, 255, 255, 254 ) } ) == 0, "a /31 has no broadcast" );
	Expect( LAN_DirectedBroadcast( { Ip( 10, 0, 0, 1 ), 0xffffffffu } ) == 0, "a /32 has no broadcast" );
	Expect( LAN_DirectedBroadcast( { Ip( 10, 0, 0, 1 ), Ip( 255, 255, 255, 252 ) } ) == Ip( 10, 0, 0, 3 ), "a /30's broadcast" );

	Expect( LAN_IsPrivate( Ip( 10, 9, 8, 7 ) ) && LAN_IsPrivate( Ip( 172, 16, 0, 1 ) ) && LAN_IsPrivate( Ip( 172, 31, 255, 1 ) ) && LAN_IsPrivate( Ip( 192, 168, 0, 1 ) ), "private ranges" );
	Expect( !LAN_IsPrivate( Ip( 172, 32, 0, 1 ) ) && !LAN_IsPrivate( Ip( 8, 8, 8, 8 ) ) && !LAN_IsPrivate( Ip( 192, 169, 0, 1 ) ) && !LAN_IsPrivate( Ip( 169, 254, 1, 1 ) ), "public ranges" );
	Expect( LAN_IsValidMask( Ip( 255, 255, 255, 0 ) ) && LAN_IsValidMask( 0 ) && !LAN_IsValidMask( Ip( 255, 0, 255, 0 ) ), "contiguous masks" );

	std::vector< uint32_t > targets;

	// a home /24: every host but ours, the network and the broadcast address
	{
		const lanSubnet_t home[] = { { Ip( 192, 168, 1, 20 ), Ip( 255, 255, 255, 0 ) } };
		LAN_SweepTargets( home, 1, 4096, targets );
		Expect( targets.size() == 253, "a /24 sweeps 253 hosts" );
		Expect( !Has( targets, Ip( 192, 168, 1, 20 ) ), "never our own address" );
		Expect( !Has( targets, Ip( 192, 168, 1, 0 ) ) && !Has( targets, Ip( 192, 168, 1, 255 ) ), "never network or broadcast" );
		Expect( Has( targets, Ip( 192, 168, 1, 1 ) ) && Has( targets, Ip( 192, 168, 1, 254 ) ), "the ends of the range" );
		Expect( Unique( targets ), "a /24 without duplicates" );
	}

	// a /16 is cut to the cap, around our address, our /24 first
	{
		const lanSubnet_t big[] = { { Ip( 10, 20, 37, 9 ), Ip( 255, 255, 0, 0 ) } };
		LAN_SweepTargets( big, 1, 1024, targets );
		Expect( targets.size() <= 1024 && targets.size() >= 1020, "a /16 is capped" );
		Expect( Unique( targets ), "a capped subnet without duplicates" );
		bool ownFirst = true;
		for ( size_t i = 0; i < 255 && i < targets.size(); i++ ) {
			ownFirst = ownFirst && ( targets[ i ] & 0xffffff00u ) == Ip( 10, 20, 37, 0 );
		}
		Expect( ownFirst, "our /24 goes first" );
		bool inBlock = true;
		for ( uint32_t t : targets ) {
			inBlock = inBlock && ( t & Ip( 255, 255, 252, 0 ) ) == Ip( 10, 20, 36, 0 );
		}
		Expect( inBlock, "the capped sweep stays in the block around our address" );
	}

	// public, point-to-point and broken subnets are not swept; overlaps are swept once
	{
		const lanSubnet_t mixed[] = {
			{ Ip( 203, 0, 113, 5 ), Ip( 255, 255, 255, 0 ) },		// public
			{ Ip( 10, 0, 0, 1 ), 0xffffffffu },						// /32
			{ Ip( 10, 0, 1, 1 ), Ip( 255, 0, 255, 0 ) },			// not contiguous
			{ Ip( 192, 168, 5, 10 ), Ip( 255, 255, 255, 0 ) },
			{ Ip( 192, 168, 5, 11 ), Ip( 255, 255, 255, 0 ) },		// a second address on it
		};
		LAN_SweepTargets( mixed, 5, 4096, targets );
		Expect( targets.size() == 252, "only the private /24, once, without our two addresses" );
		Expect( Unique( targets ), "overlapping subnets without duplicates" );
		Expect( !Has( targets, Ip( 192, 168, 5, 11 ) ) && !Has( targets, Ip( 192, 168, 5, 10 ) ), "none of our addresses" );
	}

	// the cap holds across subnets, and a cap of 0 sweeps nothing
	{
		const lanSubnet_t two[] = { { Ip( 192, 168, 1, 2 ), Ip( 255, 255, 255, 0 ) }, { Ip( 192, 168, 2, 2 ), Ip( 255, 255, 255, 0 ) } };
		LAN_SweepTargets( two, 2, 300, targets );
		Expect( targets.size() == 300, "the cap holds across subnets" );
		LAN_SweepTargets( two, 2, 0, targets );
		Expect( targets.empty(), "a cap of 0 sweeps nothing" );
	}

	if ( g_failures == 0 ) {
		printf( "LanDiscoveryTest: all checks passed\n" );
	}
	return g_failures == 0 ? 0 : 1;
}
