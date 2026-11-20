// SPDX-License-Identifier: GPL-3.0-or-later
/*
SDL3 gamepad backend.

Before this, no platform fed the controller manager: nothing called
sdControllerManager::Init on Linux or macOS, the Windows XInput and DirectInput backends
were never polled, and the game's controller input modes (InputMode.cpp) always read
zero axes. This backend opens every SDL3 gamepad (Xbox, PlayStation, Switch Pro, the
Steam Deck's built-in controls and Steam Input's virtual pads), hot-plugs them into four
fixed slots, and each frame:

  - sends button changes as controller button events (joy<slot>_<n> key names; the
    layout is in GamepadMapping.h), the triggers as buttons with hysteresis;
  - writes the sticks, the analog triggers and, when enabled, the gyro into the
    controller's axis array, which the game reads when it builds a usercmd;
  - while a menu cursor shows (the mouse is not captured), drives the menu instead: the
    left stick moves the pointer, A clicks, B goes back, the right stick scrolls and the
    d-pad sends the arrow keys. A button still held when the menu closes sends nothing
    until it is released, so the A that pressed "Spawn" does not also jump.

Start always sends Escape (in_gamepadStartIsEscape), which opens and closes the menu.

Slot hashes. The engine maps controllers to joy1-joy4 by a hash (in_joyN_device), whose
retail default is the hash of "XInput controller 0". Slot n uses the hash of
"XInput controller <n>", so the default mapping puts the first gamepad on joy1 with no
setup, and a profile made with retail's XInput mapping keeps working.
*/

#include "framework/precompiled.h"
#include <SDL3/SDL.h>
#include "sys_local.h"
#include "GamepadMapping.h"

static_assert( SYS_GP_EVENT_BUTTON1 == C_BUTTON1, "GamepadMapping.h: button 1" );
static_assert( SYS_GP_EVENT_BUTTON1 + 21 <= C_NUMBERED_BUTTON_MAX, "GamepadMapping.h: numbered buttons" );
static_assert( SYS_GP_EVENT_LEFT_TRIGGER == C_LEFT_TRIGGER, "GamepadMapping.h: left trigger" );
static_assert( SYS_GP_EVENT_RIGHT_TRIGGER == C_RIGHT_TRIGGER, "GamepadMapping.h: right trigger" );
static_assert( SYS_GP_EVENT_DPAD_UP == C_DPAD_UP, "GamepadMapping.h: d-pad up" );
static_assert( SYS_GP_EVENT_DPAD_DOWN == C_DPAD_DOWN, "GamepadMapping.h: d-pad down" );
static_assert( SYS_GP_EVENT_DPAD_LEFT == C_DPAD_LEFT, "GamepadMapping.h: d-pad left" );
static_assert( SYS_GP_EVENT_DPAD_RIGHT == C_DPAD_RIGHT, "GamepadMapping.h: d-pad right" );
static_assert( SYS_GP_AXIS_COUNT <= MAX_CONTROLLER_AXES, "GamepadMapping.h: axes" );
static_assert( SYS_GP_SOUTH == SDL_GAMEPAD_BUTTON_SOUTH, "GamepadMapping.h: SDL buttons" );
static_assert( SYS_GP_DPAD_RIGHT == SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "GamepadMapping.h: SDL buttons" );
static_assert( SYS_GP_TOUCHPAD == SDL_GAMEPAD_BUTTON_TOUCHPAD, "GamepadMapping.h: SDL buttons" );
static_assert( SYS_GP_BUTTON_COUNT == SDL_GAMEPAD_BUTTON_COUNT, "GamepadMapping.h: SDL buttons" );

SDL_Window* Sys_GetGameSDLWindow( void );

idCVar in_gamepad( "in_gamepad", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "read gamepads (SDL3); takes effect at the next start" );
idCVar in_gamepadTriggerPress( "in_gamepadTriggerPress", "0.3", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "how far a trigger is pulled (0-1) before it counts as pressed", 0.05f, 1.0f );
idCVar in_gamepadTriggerRelease( "in_gamepadTriggerRelease", "0.2", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "a pressed trigger counts as released below this (0-1)", 0.0f, 1.0f );
idCVar in_gamepadGyro( "in_gamepadGyro", "0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL | CVAR_PROFILE, "send the gamepad's gyro to the game (axes 6 and 7), for gyro aiming" );
idCVar in_gamepadStartIsEscape( "in_gamepadStartIsEscape", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "the start/menu button sends Escape (opens and closes the menu) instead of a bindable joy button" );
idCVar in_gamepadMenuCursor( "in_gamepadMenuCursor", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "while a menu cursor shows, the left stick moves it, A clicks, B goes back, the right stick scrolls" );
idCVar in_gamepadMenuCursorSpeed( "in_gamepadMenuCursorSpeed", "1400", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "menu cursor speed at full stick, in window pixels a second", 100.0f, 10000.0f );
idCVar in_gamepadMenuDeadZone( "in_gamepadMenuDeadZone", "0.2", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "stick dead zone for the menu cursor and scrolling (0-0.95)", 0.0f, 0.95f );
idCVar in_gamepadLog( "in_gamepadLog", "0", CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT, "log gamepads as they connect and disconnect, and each button change" );

namespace {

const int SDL_GAMEPAD_SLOTS = 4;

class sdControllerAPISDL;
// the live API, for the event pump; cleared when the API shuts down
sdControllerAPISDL* sdlGamepadAPI = NULL;

// the window shows a menu cursor (or the console): the mouse is not captured
bool MenuMode() {
	if ( !in_gamepadMenuCursor.GetBool() ) {
		return false;
	}
	SDL_Window* window = Sys_GetGameSDLWindow();
	if ( window == NULL ) {
		return false;
	}
	if ( ( SDL_GetWindowFlags( window ) & SDL_WINDOW_INPUT_FOCUS ) == 0 ) {
		return false;
	}
	return !SDL_GetWindowRelativeMouseMode( window );
}

class sdSDLGamepad : public sdController {
public:
	explicit sdSDLGamepad( int slot ) :
		pad( NULL ),
		padId( 0 ),
		numEvents( 0 ),
		wasMenuMode( false ),
		leftTriggerDown( false ),
		rightTriggerDown( false ),
		gyroEnabled( false ),
		wheelTime( 0.0f ) {
		this->slot = slot;
		// the retail XInput name: in_joyN_device's default maps slot 0 to joy1 (see above)
		hash = idStr::Hash( va( "XInput controller %d", slot ) ) & 0x7fffffff;
		idStr::snPrintf( name, sizeof( name ), "Gamepad %d (none)", slot + 1 );
		state = CS_NOT_CONNECTED;
		memset( buttonDown, 0, sizeof( buttonDown ) );
		memset( suppressed, 0, sizeof( suppressed ) );
		memset( menuKeyDown, 0, sizeof( menuKeyDown ) );
		memset( events, 0, sizeof( events ) );
	}

	virtual ~sdSDLGamepad() {
		Close();
	}

	bool IsOpen() const { return pad != NULL; }
	SDL_JoystickID GetPadId() const { return padId; }

	void Open( SDL_JoystickID id ) {
		pad = SDL_OpenGamepad( id );
		if ( pad == NULL ) {
			common->Warning( "gamepad: couldn't open %u: %s", static_cast< unsigned int >( id ), SDL_GetError() );
			return;
		}
		padId = id;
		const char* padName = SDL_GetGamepadName( pad );
		idStr::snPrintf( name, sizeof( name ), "Gamepad %d (%s)", slot + 1, padName != NULL ? padName : "unknown" );
		state = CS_OK;
		gyroEnabled = false;
		wasMenuMode = MenuMode();
		common->Printf( "gamepad: %s connected%s\n", name,
			SDL_GamepadHasSensor( pad, SDL_SENSOR_GYRO ) ? ", has a gyro" : "" );
	}

	void Close() {
		if ( pad == NULL ) {
			return;
		}
		ReleaseAll();
		SDL_CloseGamepad( pad );
		pad = NULL;
		padId = 0;
		common->Printf( "gamepad: %s disconnected\n", name );
		idStr::snPrintf( name, sizeof( name ), "Gamepad %d (none)", slot + 1 );
		state = CS_NOT_CONNECTED;
	}

	virtual void UpdateState() {
		state = pad != NULL ? CS_OK : CS_NOT_CONNECTED;
	}

	virtual int PollInputEvents() {
		numEvents = 0;
		if ( pad == NULL ) {
			return 0;
		}
		const float seconds = FrameSeconds();
		const bool menuMode = MenuMode();
		if ( menuMode != wasMenuMode ) {
			// what is held at the switch goes no further until it is released
			ReleaseAll();
			for ( int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++ ) {
				suppressed[ b ] = SDL_GetGamepadButton( pad, static_cast< SDL_GamepadButton >( b ) );
				buttonDown[ b ] = suppressed[ b ];
			}
			leftTriggerDown = rightTriggerDown = false;
			suppressedLeftTrigger = TriggerValue( SDL_GAMEPAD_AXIS_LEFT_TRIGGER ) >= in_gamepadTriggerRelease.GetFloat();
			suppressedRightTrigger = TriggerValue( SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ) >= in_gamepadTriggerRelease.GetFloat();
			wasMenuMode = menuMode;
		}

		UpdateButtons( menuMode );
		UpdateTriggers( menuMode );
		UpdateAxes( menuMode );
		if ( menuMode ) {
			MenuCursor( seconds );
		}
		return numEvents;
	}

	virtual int GetNumEvents() { return numEvents; }

	virtual int ReturnInputEvent( const int n, int& action, int& value ) {
		if ( n < 0 || n >= numEvents ) {
			return 0;
		}
		action = events[ n ].event;
		value = events[ n ].value;
		return 1;
	}

	virtual void EndInputEvents() { numEvents = 0; }

private:
	struct event_t {
		int event;
		int value;
	};

	enum menuKey_t { MK_CLICK, MK_BACK, MK_UP, MK_DOWN, MK_LEFT, MK_RIGHT, MK_COUNT };

	float FrameSeconds() {
		const Uint64 now = SDL_GetTicksNS();
		float seconds = 0.0f;
		if ( lastPollNs != 0 && now > lastPollNs ) {
			seconds = static_cast< float >( static_cast< double >( now - lastPollNs ) * 1e-9 );
		}
		lastPollNs = now;
		return idMath::ClampFloat( 0.0f, 0.1f, seconds );
	}

	float TriggerValue( SDL_GamepadAxis axisId ) const {
		const float value = Sys_GamepadAxisToFloat( SDL_GetGamepadAxis( pad, axisId ) );
		return value < 0.0f ? 0.0f : value;
	}

	void PostControllerEvent( int event, bool down ) {
		if ( in_gamepadLog.GetBool() ) {
			common->Printf( "gamepad: %s event %d %s%s\n", name, event, down ? "down" : "up", mapped ? "" : " (no joy slot)" );
		}
		if ( !mapped ) {
			return;
		}
		sys->QueEvent( SE_CONTROLLER_BUTTON, event, ( hash << 1 ) | ( down ? 1 : 0 ), 0, NULL );
		if ( numEvents < MAX_EVENTS ) {
			events[ numEvents ].event = event;
			events[ numEvents ].value = down ? 1 : 0;
			numEvents++;
		}
	}

	void PostKey( keyNum_t key, bool down ) {
		sys->QueEvent( SE_KEY, key, down ? 1 : 0, 0, NULL );
	}

	void PostMenuKey( menuKey_t which, bool down ) {
		if ( menuKeyDown[ which ] == down ) {
			return;
		}
		menuKeyDown[ which ] = down;
		switch ( which ) {
			case MK_CLICK:	sys->QueEvent( SE_MOUSE_BUTTON, M_MOUSE1, down ? 1 : 0, 0, NULL ); break;
			case MK_BACK:	PostKey( K_ESCAPE, down ); break;
			case MK_UP:		PostKey( K_UPARROW, down ); break;
			case MK_DOWN:	PostKey( K_DOWNARROW, down ); break;
			case MK_LEFT:	PostKey( K_LEFTARROW, down ); break;
			case MK_RIGHT:	PostKey( K_RIGHTARROW, down ); break;
			default:		break;
		}
	}

	// the menu meaning of a button, or -1
	static int MenuKeyForButton( int button ) {
		switch ( button ) {
			case SDL_GAMEPAD_BUTTON_SOUTH:		return MK_CLICK;
			case SDL_GAMEPAD_BUTTON_EAST:		return MK_BACK;
			case SDL_GAMEPAD_BUTTON_DPAD_UP:	return MK_UP;
			case SDL_GAMEPAD_BUTTON_DPAD_DOWN:	return MK_DOWN;
			case SDL_GAMEPAD_BUTTON_DPAD_LEFT:	return MK_LEFT;
			case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:	return MK_RIGHT;
			default:							return -1;
		}
	}

	void UpdateButtons( bool menuMode ) {
		for ( int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++ ) {
			const bool down = SDL_GetGamepadButton( pad, static_cast< SDL_GamepadButton >( b ) );
			if ( down == buttonDown[ b ] ) {
				continue;
			}
			buttonDown[ b ] = down;
			if ( suppressed[ b ] ) {
				if ( !down ) {
					suppressed[ b ] = false;
				}
				continue;
			}
			if ( b == SDL_GAMEPAD_BUTTON_START && in_gamepadStartIsEscape.GetBool() ) {
				PostKey( K_ESCAPE, down );
				continue;
			}
			if ( menuMode ) {
				const int menuKey = MenuKeyForButton( b );
				if ( menuKey >= 0 ) {
					PostMenuKey( static_cast< menuKey_t >( menuKey ), down );
				}
				continue;
			}
			const int event = Sys_GamepadButtonEvent( b );
			if ( event != SYS_GP_EVENT_NONE ) {
				PostControllerEvent( event, down );
			}
		}
	}

	void UpdateTriggers( bool menuMode ) {
		const float pressAt = in_gamepadTriggerPress.GetFloat();
		const float releaseAt = in_gamepadTriggerRelease.GetFloat();
		const float left = TriggerValue( SDL_GAMEPAD_AXIS_LEFT_TRIGGER );
		const float right = TriggerValue( SDL_GAMEPAD_AXIS_RIGHT_TRIGGER );
		if ( suppressedLeftTrigger && left < releaseAt ) {
			suppressedLeftTrigger = false;
		}
		if ( suppressedRightTrigger && right < releaseAt ) {
			suppressedRightTrigger = false;
		}
		const bool leftDown = !suppressedLeftTrigger && Sys_GamepadTriggerLatch( leftTriggerDown, left, pressAt, releaseAt );
		const bool rightDown = !suppressedRightTrigger && Sys_GamepadTriggerLatch( rightTriggerDown, right, pressAt, releaseAt );
		if ( leftDown != leftTriggerDown ) {
			leftTriggerDown = leftDown;
			if ( !menuMode ) {
				PostControllerEvent( C_LEFT_TRIGGER, leftDown );
			}
		}
		if ( rightDown != rightTriggerDown ) {
			rightTriggerDown = rightDown;
			if ( !menuMode ) {
				PostControllerEvent( C_RIGHT_TRIGGER, rightDown );
			}
		}
	}

	void UpdateAxes( bool menuMode ) {
		if ( menuMode ) {
			// the sticks drive the menu, not the player
			memset( axis, 0, sizeof( axis ) );
			return;
		}
		axis[ SYS_GP_AXIS_RIGHT_X ] = Sys_GamepadAxisToFloat( SDL_GetGamepadAxis( pad, SDL_GAMEPAD_AXIS_RIGHTX ) );
		axis[ SYS_GP_AXIS_RIGHT_Y ] = Sys_GamepadAxisToFloat( SDL_GetGamepadAxis( pad, SDL_GAMEPAD_AXIS_RIGHTY ) );
		axis[ SYS_GP_AXIS_LEFT_X ] = Sys_GamepadAxisToFloat( SDL_GetGamepadAxis( pad, SDL_GAMEPAD_AXIS_LEFTX ) );
		axis[ SYS_GP_AXIS_LEFT_Y ] = Sys_GamepadAxisToFloat( SDL_GetGamepadAxis( pad, SDL_GAMEPAD_AXIS_LEFTY ) );
		axis[ SYS_GP_AXIS_LEFT_TRIGGER ] = TriggerValue( SDL_GAMEPAD_AXIS_LEFT_TRIGGER );
		axis[ SYS_GP_AXIS_RIGHT_TRIGGER ] = TriggerValue( SDL_GAMEPAD_AXIS_RIGHT_TRIGGER );

		const bool wantGyro = in_gamepadGyro.GetBool() && SDL_GamepadHasSensor( pad, SDL_SENSOR_GYRO );
		if ( wantGyro != gyroEnabled ) {
			gyroEnabled = SDL_SetGamepadSensorEnabled( pad, SDL_SENSOR_GYRO, wantGyro ) && wantGyro;
		}
		axis[ SYS_GP_AXIS_GYRO_YAW ] = 0.0f;
		axis[ SYS_GP_AXIS_GYRO_PITCH ] = 0.0f;
		float gyro[ 3 ] = { 0.0f, 0.0f, 0.0f };
		if ( gyroEnabled && SDL_GetGamepadSensorData( pad, SDL_SENSOR_GYRO, gyro, 3 ) ) {
			// radians a second: [0] pitch (top edge towards you +), [1] yaw (turning left +)
			const float scale = RAD2DEG( 1.0f ) / SYS_GAMEPAD_GYRO_FULL_SCALE_DPS;
			axis[ SYS_GP_AXIS_GYRO_YAW ] = idMath::ClampFloat( -1.0f, 1.0f, -gyro[ 1 ] * scale );
			axis[ SYS_GP_AXIS_GYRO_PITCH ] = idMath::ClampFloat( -1.0f, 1.0f, -gyro[ 0 ] * scale );
		}
	}

	void MenuCursor( float seconds ) {
		SDL_Window* window = Sys_GetGameSDLWindow();
		if ( window == NULL ) {
			return;
		}
		const float deadZone = in_gamepadMenuDeadZone.GetFloat();
		const float lx = Sys_GamepadAxisToFloat( SDL_GetGamepadAxis( pad, SDL_GAMEPAD_AXIS_LEFTX ) );
		const float ly = Sys_GamepadAxisToFloat( SDL_GetGamepadAxis( pad, SDL_GAMEPAD_AXIS_LEFTY ) );
		float dx = 0.0f;
		float dy = 0.0f;
		Sys_GamepadCursorDelta( lx, ly, deadZone, in_gamepadMenuCursorSpeed.GetFloat(), seconds, dx, dy );
		if ( dx != 0.0f || dy != 0.0f ) {
			int width = 0;
			int height = 0;
			SDL_GetWindowSize( window, &width, &height );
			float px = 0.0f;
			float py = 0.0f;
			SDL_GetMouseState( &px, &py );
			// keep the sub-pixel part while nothing else moved the pointer
			if ( idMath::Fabs( px - lastWarpX ) < 1.0f && idMath::Fabs( py - lastWarpY ) < 1.0f ) {
				px = cursorX;
				py = cursorY;
			}
			cursorX = idMath::ClampFloat( 0.0f, static_cast< float >( width > 0 ? width - 1 : 0 ), px + dx );
			cursorY = idMath::ClampFloat( 0.0f, static_cast< float >( height > 0 ? height - 1 : 0 ), py + dy );
			SDL_WarpMouseInWindow( window, cursorX, cursorY );
			lastWarpX = idMath::Floor( cursorX );
			lastWarpY = idMath::Floor( cursorY );
		}

		// the right stick scrolls lists: one wheel step a repeat, faster the further it is pushed
		const float ry = Sys_GamepadAxisToFloat( SDL_GetGamepadAxis( pad, SDL_GAMEPAD_AXIS_RIGHTY ) );
		if ( idMath::Fabs( ry ) <= idMath::ClampFloat( 0.3f, 0.95f, deadZone + 0.2f ) ) {
			wheelTime = 0.0f;
			return;
		}
		wheelTime -= seconds;
		if ( wheelTime > 0.0f ) {
			return;
		}
		wheelTime = 0.25f - 0.17f * idMath::ClampFloat( 0.0f, 1.0f, idMath::Fabs( ry ) );
		const mouseButton_t wheel = ry < 0.0f ? M_MWHEELUP : M_MWHEELDOWN;
		sys->QueEvent( SE_MOUSE_BUTTON, wheel, 1, 0, NULL );
		sys->QueEvent( SE_MOUSE_BUTTON, wheel, 0, 0, NULL );
	}

	// lift everything this pad holds down, so no key or button sticks on a switch or unplug
	void ReleaseAll() {
		for ( int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++ ) {
			if ( !buttonDown[ b ] ) {
				continue;
			}
			buttonDown[ b ] = false;
			if ( suppressed[ b ] ) {
				suppressed[ b ] = false;
				continue;
			}
			if ( b == SDL_GAMEPAD_BUTTON_START && in_gamepadStartIsEscape.GetBool() ) {
				PostKey( K_ESCAPE, false );
				continue;
			}
			const int event = Sys_GamepadButtonEvent( b );
			if ( event != SYS_GP_EVENT_NONE && !wasMenuMode ) {
				PostControllerEvent( event, false );
			}
		}
		for ( int k = 0; k < MK_COUNT; k++ ) {
			PostMenuKey( static_cast< menuKey_t >( k ), false );
		}
		if ( leftTriggerDown && !wasMenuMode ) {
			PostControllerEvent( C_LEFT_TRIGGER, false );
		}
		if ( rightTriggerDown && !wasMenuMode ) {
			PostControllerEvent( C_RIGHT_TRIGGER, false );
		}
		leftTriggerDown = rightTriggerDown = false;
		suppressedLeftTrigger = suppressedRightTrigger = false;
		memset( axis, 0, sizeof( axis ) );
		wheelTime = 0.0f;
	}

	static const int MAX_EVENTS = 64;

	int					slot;
	SDL_Gamepad*		pad;
	SDL_JoystickID		padId;
	event_t				events[ MAX_EVENTS ];
	int					numEvents;
	bool				buttonDown[ SDL_GAMEPAD_BUTTON_COUNT ];
	bool				suppressed[ SDL_GAMEPAD_BUTTON_COUNT ];
	bool				menuKeyDown[ MK_COUNT ];
	bool				wasMenuMode;
	bool				leftTriggerDown;
	bool				rightTriggerDown;
	bool				suppressedLeftTrigger = false;
	bool				suppressedRightTrigger = false;
	bool				gyroEnabled;
	float				wheelTime;
	Uint64				lastPollNs = 0;
	// the menu cursor, kept in floats between frames (SDL_GetMouseState rounds)
	float				cursorX = 0.0f;
	float				cursorY = 0.0f;
	float				lastWarpX = -10.0f;
	float				lastWarpY = -10.0f;
};

class sdControllerAPISDL : public sdControllerAPI {
public:
	sdControllerAPISDL() : apiIndex( -1 ) {
		memset( slots, 0, sizeof( slots ) );
	}

	virtual const char* GetName() const { return "SDL3"; }
	virtual int GetMaxControllers() const { return SDL_GAMEPAD_SLOTS; }

	virtual void Init( const int index ) {
		apiIndex = index;
		if ( !in_gamepad.GetBool() ) {
			common->Printf( "controllers: SDL3 gamepads off (in_gamepad 0)\n" );
			state = CAS_INIT_FAILED;
			return;
		}
		// the Steam Deck's and other pads' own HID drivers give the paddles and the gyro
		SDL_SetHint( SDL_HINT_JOYSTICK_HIDAPI, "1" );
		if ( !SDL_InitSubSystem( SDL_INIT_GAMEPAD ) ) {
			common->Warning( "controllers: SDL3 gamepad init failed: %s", SDL_GetError() );
			state = CAS_INIT_FAILED;
			return;
		}

		sdControllerManager& manager = sys->GetControllerManager();
		for ( int i = 0; i < SDL_GAMEPAD_SLOTS; i++ ) {
			slots[ i ] = new sdSDLGamepad( i );
			slots[ i ]->SetAPITypeIndex( index );
			slots[ i ]->SetIndex( i );
			manager.AddController( *slots[ i ] );
		}

		int count = 0;
		SDL_JoystickID* ids = SDL_GetGamepads( &count );
		if ( ids != NULL ) {
			for ( int i = 0; i < count; i++ ) {
				Added( ids[ i ] );
			}
			SDL_free( ids );
		}
		state = CAS_SUPPORTED;
		common->Printf( "controllers: SDL3 initialized, %d gamepad%s connected\n", count, count == 1 ? "" : "s" );
	}

	virtual void Shutdown() {
		// the manager deletes the controllers (sdControllerManagerLocal::Shutdown)
		for ( int i = 0; i < SDL_GAMEPAD_SLOTS; i++ ) {
			if ( slots[ i ] != NULL ) {
				slots[ i ]->Close();
			}
			slots[ i ] = NULL;
		}
		if ( state == CAS_SUPPORTED ) {
			SDL_QuitSubSystem( SDL_INIT_GAMEPAD );
		}
		if ( sdlGamepadAPI == this ) {
			sdlGamepadAPI = NULL;
		}
		sdControllerAPI::Shutdown();
	}

	void Added( SDL_JoystickID id ) {
		if ( state != CAS_SUPPORTED && slots[ 0 ] == NULL ) {
			return;
		}
		for ( int i = 0; i < SDL_GAMEPAD_SLOTS; i++ ) {
			if ( slots[ i ] != NULL && slots[ i ]->IsOpen() && slots[ i ]->GetPadId() == id ) {
				return;	// already open (listed at init, then announced)
			}
		}
		for ( int i = 0; i < SDL_GAMEPAD_SLOTS; i++ ) {
			if ( slots[ i ] != NULL && !slots[ i ]->IsOpen() ) {
				slots[ i ]->Open( id );
				return;
			}
		}
		common->Printf( "gamepad: no free slot for %s (at most %d)\n", SDL_GetGamepadNameForID( id ), SDL_GAMEPAD_SLOTS );
	}

	void Removed( SDL_JoystickID id ) {
		for ( int i = 0; i < SDL_GAMEPAD_SLOTS; i++ ) {
			if ( slots[ i ] != NULL && slots[ i ]->IsOpen() && slots[ i ]->GetPadId() == id ) {
				slots[ i ]->Close();
				return;
			}
		}
	}

	void Poll() {
		if ( state != CAS_SUPPORTED ) {
			return;
		}
		for ( int i = 0; i < SDL_GAMEPAD_SLOTS; i++ ) {
			if ( slots[ i ] != NULL ) {
				slots[ i ]->PollInputEvents();
				slots[ i ]->EndInputEvents();
			}
		}
	}

private:
	int					apiIndex;
	sdSDLGamepad*		slots[ SDL_GAMEPAD_SLOTS ];
};

} // namespace

/*
Called by sdControllerManagerLocal::Init. Returns the API, initialized; the manager owns
it. NULL when the build has no SDL3 client layer (sys_input.cpp has the fallback).
*/
sdControllerAPI* Sys_CreateSDLGamepadAPI( int apiIndex ) {
	sdControllerAPISDL* api = new sdControllerAPISDL;
	sdlGamepadAPI = api;
	api->Init( apiIndex );
	return api;
}

// SDL_EVENT_GAMEPAD_ADDED and _REMOVED, from Sys_ProcessSDLEvents
void Sys_SDLGamepadDeviceEvent( const SDL_Event& event ) {
	if ( sdlGamepadAPI == NULL ) {
		return;
	}
	if ( event.type == SDL_EVENT_GAMEPAD_ADDED ) {
		sdlGamepadAPI->Added( event.gdevice.which );
	} else if ( event.type == SDL_EVENT_GAMEPAD_REMOVED ) {
		sdlGamepadAPI->Removed( event.gdevice.which );
	}
}

// each frame after the SDL events are read
void Sys_PollSDLGamepads( void ) {
	if ( sdlGamepadAPI != NULL ) {
		sdlGamepadAPI->Poll();
	}
}
