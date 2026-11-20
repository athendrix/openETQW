// SPDX-License-Identifier: GPL-3.0-or-later
/*
===========================================================================
openETQW Asset-Free Test Harness: GamepadMappingTest.cpp
The SDL3 gamepad backend's pure helpers (src/sys/sdl/GamepadMapping.h): the button
layout keeps the retail XInput numbering (joy1_1 is still A), every SDL button has a
distinct event, triggers latch with hysteresis, and the menu cursor has a radial dead
zone and no faster diagonals.
===========================================================================
*/

#include "GamepadMapping.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int failures = 0;

static void Expect( bool ok, const char* what ) {
	if ( !ok ) {
		std::printf( "FAIL: %s\n", what );
		failures++;
	}
}

static bool Near( float a, float b, float eps = 1e-4f ) {
	return std::fabs( a - b ) <= eps;
}

int main() {
	// the retail XInput layout (win_xinput.cpp XInputButtonRemap): joy1_1..joy1_10
	Expect( Sys_GamepadButtonEvent( SYS_GP_SOUTH ) == 0, "A is button 1" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_EAST ) == 1, "B is button 2" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_WEST ) == 2, "X is button 3" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_NORTH ) == 3, "Y is button 4" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_LEFT_SHOULDER ) == 4, "LB is button 5" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_RIGHT_SHOULDER ) == 5, "RB is button 6" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_BACK ) == 6, "back is button 7" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_START ) == 7, "start is button 8" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_LEFT_STICK ) == 8, "left stick click is button 9" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_RIGHT_STICK ) == 9, "right stick click is button 10" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_DPAD_UP ) == SYS_GP_EVENT_DPAD_UP, "d-pad up" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_DPAD_RIGHT ) == SYS_GP_EVENT_DPAD_RIGHT, "d-pad right" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_LEFT_PADDLE1 ) == 13, "Deck L4 is button 14" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_RIGHT_PADDLE1 ) == 12, "Deck R4 is button 13" );
	Expect( Sys_GamepadButtonEvent( -1 ) == SYS_GP_EVENT_NONE, "invalid button" );
	Expect( Sys_GamepadButtonEvent( SYS_GP_BUTTON_COUNT ) == SYS_GP_EVENT_NONE, "button past the end" );

	// every SDL button maps to its own event, inside the engine's button range (0..37)
	bool seen[ 64 ] = {};
	bool distinct = true;
	bool inRange = true;
	for ( int b = 0; b < SYS_GP_BUTTON_COUNT; b++ ) {
		const int e = Sys_GamepadButtonEvent( b );
		if ( e < 0 || e > SYS_GP_EVENT_DPAD_RIGHT ) {
			inRange = false;
			continue;
		}
		if ( seen[ e ] ) {
			distinct = false;
		}
		seen[ e ] = true;
		// numbered buttons stay below the trigger events
		if ( b != SYS_GP_DPAD_UP && b != SYS_GP_DPAD_DOWN && b != SYS_GP_DPAD_LEFT && b != SYS_GP_DPAD_RIGHT && e >= SYS_GP_EVENT_LEFT_TRIGGER ) {
			inRange = false;
		}
	}
	Expect( distinct, "each button has its own event" );
	Expect( inRange, "events stay in the engine's button range" );

	// axis scaling, both ends reach exactly 1
	Expect( Sys_GamepadAxisToFloat( 0 ) == 0.0f, "axis 0" );
	Expect( Sys_GamepadAxisToFloat( 32767 ) == 1.0f, "axis max" );
	Expect( Sys_GamepadAxisToFloat( -32768 ) == -1.0f, "axis min" );
	Expect( Near( Sys_GamepadAxisToFloat( 16384 ), 0.5f, 1e-3f ), "axis half" );

	// trigger hysteresis: press at 0.3, hold down to 0.2
	Expect( !Sys_GamepadTriggerLatch( false, 0.25f, 0.3f, 0.2f ), "below press stays up" );
	Expect( Sys_GamepadTriggerLatch( false, 0.3f, 0.3f, 0.2f ), "press at the threshold" );
	Expect( Sys_GamepadTriggerLatch( true, 0.25f, 0.3f, 0.2f ), "held between release and press stays down" );
	Expect( !Sys_GamepadTriggerLatch( true, 0.19f, 0.3f, 0.2f ), "below release lets go" );
	// release (0.3) above press (0.2) is clamped to 0.2: held at 0.25 stays down, 0.15 lets go
	Expect( Sys_GamepadTriggerLatch( true, 0.25f, 0.2f, 0.3f ), "release above press is clamped to press" );
	Expect( !Sys_GamepadTriggerLatch( true, 0.15f, 0.2f, 0.3f ), "below the clamped release lets go" );
	Expect( !Sys_GamepadTriggerLatch( true, std::numeric_limits< float >::quiet_NaN(), 0.3f, 0.2f ), "NaN lets go" );

	// menu cursor: radial dead zone, quadratic, diagonal no faster than straight
	float dx = 1.0f, dy = 1.0f;
	Sys_GamepadCursorDelta( 0.15f, 0.15f, 0.25f, 1000.0f, 0.016f, dx, dy );
	Expect( dx == 0.0f && dy == 0.0f, "inside the radial dead zone (0.21 < 0.25)" );
	Sys_GamepadCursorDelta( 1.0f, 0.0f, 0.2f, 1000.0f, 0.5f, dx, dy );
	Expect( Near( dx, 500.0f ) && dy == 0.0f, "full right for half a second" );
	float ddx = 0.0f, ddy = 0.0f;
	Sys_GamepadCursorDelta( 1.0f, 1.0f, 0.2f, 1000.0f, 0.5f, ddx, ddy );
	Expect( Near( std::sqrt( ddx * ddx + ddy * ddy ), 500.0f, 0.01f ), "full diagonal is no faster" );
	Sys_GamepadCursorDelta( 0.6f, 0.0f, 0.2f, 1000.0f, 1.0f, dx, dy );
	Expect( Near( dx, 250.0f, 0.01f ), "half way past the dead zone is a quarter speed" );
	Sys_GamepadCursorDelta( 0.0f, -1.0f, 0.2f, 1000.0f, 0.1f, dx, dy );
	Expect( dx == 0.0f && Near( dy, -100.0f ), "up is negative" );
	Sys_GamepadCursorDelta( 1.0f, 0.0f, 0.2f, 1000.0f, 0.0f, dx, dy );
	Expect( dx == 0.0f && dy == 0.0f, "no time, no motion" );
	Sys_GamepadCursorDelta( std::numeric_limits< float >::infinity(), 0.0f, 0.2f, 1000.0f, 0.1f, dx, dy );
	Expect( dx == 0.0f && dy == 0.0f, "non-finite stick" );

	if ( failures != 0 ) {
		std::printf( "%d failure(s)\n", failures );
		return 1;
	}
	std::printf( "gamepad mapping: all checks passed\n" );
	return 0;
}
