// SPDX-License-Identifier: GPL-3.0-or-later
/*
===========================================================================
openETQW Asset-Free Test Harness: SteamDeckTest.cpp
Steam Deck detection (src/sys/SteamDeck.h): the override wins, then Steam's SteamDeck=1,
then the Deck's GPU IDs; the first-run settings keep ray tracing off.
===========================================================================
*/

#include "SteamDeck.h"

#include <cstdio>
#include <cstring>

static int failures = 0;

static void Expect( bool ok, const char* what ) {
	if ( !ok ) {
		std::printf( "FAIL: %s\n", what );
		failures++;
	}
}

int main() {
	Expect( !Sys_IsSteamDeckFrom( NULL, NULL, 0, 0 ), "nothing set is not a Deck" );
	Expect( Sys_IsSteamDeckFrom( NULL, "1", 0, 0 ), "Steam's SteamDeck=1" );
	Expect( !Sys_IsSteamDeckFrom( NULL, "0", 0, 0 ), "SteamDeck=0" );
	Expect( !Sys_IsSteamDeckFrom( NULL, "yes", 0, 0 ), "an unexpected value says nothing" );
	Expect( Sys_IsSteamDeckFrom( NULL, NULL, 0x1002, 0x163f ), "the LCD Deck's GPU" );
	Expect( Sys_IsSteamDeckFrom( NULL, NULL, 0x1002, 0x1435 ), "the OLED Deck's GPU" );
	Expect( !Sys_IsSteamDeckFrom( NULL, NULL, 0x1002, 0x73ff ), "another AMD GPU" );
	Expect( !Sys_IsSteamDeckFrom( NULL, NULL, 0x10de, 0x163f ), "the device ID on another vendor" );
	Expect( !Sys_IsSteamDeckFrom( "0", "1", 0x1002, 0x163f ), "the override turns it off" );
	Expect( Sys_IsSteamDeckFrom( "1", NULL, 0, 0 ), "the override turns it on" );
	Expect( Sys_IsSteamDeckFrom( "", "1", 0, 0 ), "an empty override says nothing" );
	Expect( std::strstr( STEAMDECK_DISPLAY_DEFAULTS, "set r_rayTracing 0\n" ) != NULL, "ray tracing off" );
	Expect( std::strstr( STEAMDECK_DISPLAY_DEFAULTS, "set r_customWidth 1280\n" ) != NULL &&
		std::strstr( STEAMDECK_DISPLAY_DEFAULTS, "set r_customHeight 800\n" ) != NULL, "native 1280x800" );
	Expect( STEAMDECK_MACHINE_SPEC >= 0 && STEAMDECK_MACHINE_SPEC <= 2, "a preset level" );

	if ( failures != 0 ) {
		std::printf( "%d failure(s)\n", failures );
		return 1;
	}
	std::printf( "steam deck: all checks passed\n" );
	return 0;
}
