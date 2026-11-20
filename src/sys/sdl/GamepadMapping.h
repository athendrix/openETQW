// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __SYS_SDL_GAMEPADMAPPING_H__
#define __SYS_SDL_GAMEPADMAPPING_H__

#include <cmath>

/*
Pure helpers for the SDL3 gamepad backend (sdl_gamepad.cpp), kept free of SDL and of the
engine headers so the asset-free unit suite can test them (tests/GamepadMappingTest.cpp).

Controller events and key names. The engine names a controller's buttons joy<slot>_<n>
(numbered buttons 1-32) plus joy<slot>_left_trigger, _right_trigger and _dpad_*
(sdControllerManager::GetKeyNameForSlotButton). The numbered buttons follow the retail
XInput layout (win_xinput.cpp), so a retail binding of joy1_1 is still the bottom face
button: 1 A/cross, 2 B/circle, 3 X/square, 4 Y/triangle, 5 left bumper, 6 right bumper,
7 back/view, 8 start/menu, 9 left stick click, 10 right stick click. SDL3 adds the rest:
11 guide, 12 misc (share, Steam Deck "..." quick access), 13 right paddle 1 (Deck R4),
14 left paddle 1 (Deck L4), 15 right paddle 2 (Deck R5), 16 left paddle 2 (Deck L5),
17 touchpad click, 18-22 misc 2-6.

Axes (sdController::axis, read by the game's input modes as controllerAxis[n]):
  0 right stick X (right +)    1 right stick Y (down +)
  2 left stick X (right +)     3 left stick Y (down +)
  4 left trigger (0..1)        5 right trigger (0..1)
  6 gyro yaw rate              7 gyro pitch rate
Indices 0-3 are the retail XInput order. The gyro axes carry an angular velocity scaled by
SYS_GAMEPAD_GYRO_FULL_SCALE_DPS (1.0 = that many degrees a second), zero when the gyro is
off or the pad has none.
*/

// the engine's sys_cEvents values, mirrored; sdl_gamepad.cpp asserts they match
enum sysGamepadEvent_t {
	SYS_GP_EVENT_BUTTON1 = 0,
	SYS_GP_EVENT_LEFT_TRIGGER = 32,
	SYS_GP_EVENT_RIGHT_TRIGGER = 33,
	SYS_GP_EVENT_DPAD_UP = 34,
	SYS_GP_EVENT_DPAD_DOWN = 35,
	SYS_GP_EVENT_DPAD_LEFT = 36,
	SYS_GP_EVENT_DPAD_RIGHT = 37,
	SYS_GP_EVENT_NONE = -1
};

// SDL_GamepadButton values, mirrored; sdl_gamepad.cpp asserts they match
enum sysGamepadButton_t {
	SYS_GP_SOUTH, SYS_GP_EAST, SYS_GP_WEST, SYS_GP_NORTH,
	SYS_GP_BACK, SYS_GP_GUIDE, SYS_GP_START,
	SYS_GP_LEFT_STICK, SYS_GP_RIGHT_STICK,
	SYS_GP_LEFT_SHOULDER, SYS_GP_RIGHT_SHOULDER,
	SYS_GP_DPAD_UP, SYS_GP_DPAD_DOWN, SYS_GP_DPAD_LEFT, SYS_GP_DPAD_RIGHT,
	SYS_GP_MISC1,
	SYS_GP_RIGHT_PADDLE1, SYS_GP_LEFT_PADDLE1, SYS_GP_RIGHT_PADDLE2, SYS_GP_LEFT_PADDLE2,
	SYS_GP_TOUCHPAD,
	SYS_GP_MISC2, SYS_GP_MISC3, SYS_GP_MISC4, SYS_GP_MISC5, SYS_GP_MISC6,
	SYS_GP_BUTTON_COUNT
};

enum {
	SYS_GP_AXIS_RIGHT_X = 0,
	SYS_GP_AXIS_RIGHT_Y = 1,
	SYS_GP_AXIS_LEFT_X = 2,
	SYS_GP_AXIS_LEFT_Y = 3,
	SYS_GP_AXIS_LEFT_TRIGGER = 4,
	SYS_GP_AXIS_RIGHT_TRIGGER = 5,
	SYS_GP_AXIS_GYRO_YAW = 6,
	SYS_GP_AXIS_GYRO_PITCH = 7,
	SYS_GP_AXIS_COUNT = 8
};

static const float SYS_GAMEPAD_GYRO_FULL_SCALE_DPS = 1000.0f;

// the controller event (sys_cEvents value) an SDL gamepad button sends, or SYS_GP_EVENT_NONE
inline int Sys_GamepadButtonEvent( int button ) {
	switch ( button ) {
		case SYS_GP_SOUTH:			return SYS_GP_EVENT_BUTTON1 + 0;
		case SYS_GP_EAST:			return SYS_GP_EVENT_BUTTON1 + 1;
		case SYS_GP_WEST:			return SYS_GP_EVENT_BUTTON1 + 2;
		case SYS_GP_NORTH:			return SYS_GP_EVENT_BUTTON1 + 3;
		case SYS_GP_LEFT_SHOULDER:	return SYS_GP_EVENT_BUTTON1 + 4;
		case SYS_GP_RIGHT_SHOULDER:	return SYS_GP_EVENT_BUTTON1 + 5;
		case SYS_GP_BACK:			return SYS_GP_EVENT_BUTTON1 + 6;
		case SYS_GP_START:			return SYS_GP_EVENT_BUTTON1 + 7;
		case SYS_GP_LEFT_STICK:		return SYS_GP_EVENT_BUTTON1 + 8;
		case SYS_GP_RIGHT_STICK:	return SYS_GP_EVENT_BUTTON1 + 9;
		case SYS_GP_GUIDE:			return SYS_GP_EVENT_BUTTON1 + 10;
		case SYS_GP_MISC1:			return SYS_GP_EVENT_BUTTON1 + 11;
		case SYS_GP_RIGHT_PADDLE1:	return SYS_GP_EVENT_BUTTON1 + 12;
		case SYS_GP_LEFT_PADDLE1:	return SYS_GP_EVENT_BUTTON1 + 13;
		case SYS_GP_RIGHT_PADDLE2:	return SYS_GP_EVENT_BUTTON1 + 14;
		case SYS_GP_LEFT_PADDLE2:	return SYS_GP_EVENT_BUTTON1 + 15;
		case SYS_GP_TOUCHPAD:		return SYS_GP_EVENT_BUTTON1 + 16;
		case SYS_GP_MISC2:			return SYS_GP_EVENT_BUTTON1 + 17;
		case SYS_GP_MISC3:			return SYS_GP_EVENT_BUTTON1 + 18;
		case SYS_GP_MISC4:			return SYS_GP_EVENT_BUTTON1 + 19;
		case SYS_GP_MISC5:			return SYS_GP_EVENT_BUTTON1 + 20;
		case SYS_GP_MISC6:			return SYS_GP_EVENT_BUTTON1 + 21;
		case SYS_GP_DPAD_UP:		return SYS_GP_EVENT_DPAD_UP;
		case SYS_GP_DPAD_DOWN:		return SYS_GP_EVENT_DPAD_DOWN;
		case SYS_GP_DPAD_LEFT:		return SYS_GP_EVENT_DPAD_LEFT;
		case SYS_GP_DPAD_RIGHT:		return SYS_GP_EVENT_DPAD_RIGHT;
		default:					return SYS_GP_EVENT_NONE;
	}
}

// an SDL stick or trigger value (-32768..32767) as -1..1
inline float Sys_GamepadAxisToFloat( int value ) {
	if ( value <= -32767 ) {
		return -1.0f;
	}
	if ( value >= 32767 ) {
		return 1.0f;
	}
	return static_cast< float >( value ) / 32767.0f;
}

/*
An analog trigger as a button with hysteresis: pressed at or above pressAt, released only
below releaseAt, so a trigger resting near the threshold does not chatter (a held zoom
that flickers in and out). releaseAt is clamped to at most pressAt.
*/
inline bool Sys_GamepadTriggerLatch( bool wasDown, float value, float pressAt, float releaseAt ) {
	if ( releaseAt > pressAt ) {
		releaseAt = pressAt;
	}
	if ( !std::isfinite( value ) ) {
		return false;
	}
	return wasDown ? value >= releaseAt : value >= pressAt;
}

/*
A stick as menu cursor motion, in pixels for this frame. The dead zone is radial (the
stick's length, not each axis), the rest is rescaled to 0..1 and squared for fine control
near the centre, and the length is clamped to 1 so diagonals are no faster.
*/
inline void Sys_GamepadCursorDelta( float x, float y, float deadZone, float pixelsPerSecond, float seconds, float& dx, float& dy ) {
	dx = 0.0f;
	dy = 0.0f;
	if ( !std::isfinite( x ) || !std::isfinite( y ) || !( seconds > 0.0f ) ) {
		return;
	}
	if ( deadZone < 0.0f ) {
		deadZone = 0.0f;
	} else if ( deadZone > 0.95f ) {
		deadZone = 0.95f;
	}
	const float length = std::sqrt( x * x + y * y );
	if ( length <= deadZone ) {
		return;
	}
	const float clamped = length > 1.0f ? 1.0f : length;
	const float t = ( clamped - deadZone ) / ( 1.0f - deadZone );
	const float speed = t * t * pixelsPerSecond * seconds;
	dx = x / length * speed;
	dy = y / length * speed;
}

#endif /* !__SYS_SDL_GAMEPADMAPPING_H__ */
