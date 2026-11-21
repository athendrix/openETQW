// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __SYS_STEAMDECK_H__
#define __SYS_STEAMDECK_H__

#include <cstdlib>
#include <cstring>

/*
Steam Deck detection and its first-run settings. Steam sets SteamDeck=1 in the
environment of everything it starts on a Deck (game mode and desktop mode alike). The
GPU IDs are the fallback once the renderer knows them: AMD 0x163f is the LCD model's
"Van Gogh" APU, 0x1435 the OLED model's "Sephiroth". OPENETQW_STEAMDECK=0 or 1 overrides
both, to test or to opt out.
*/

// the environment value says Deck: "1" is, "0" is not, anything else says nothing
inline int Sys_SteamDeckEnvValue( const char* value ) {
	if ( value == NULL || value[ 0 ] == '\0' ) {
		return -1;
	}
	if ( strcmp( value, "1" ) == 0 ) {
		return 1;
	}
	if ( strcmp( value, "0" ) == 0 ) {
		return 0;
	}
	return -1;
}

inline bool Sys_IsSteamDeckGpu( unsigned int vendorID, unsigned int deviceID ) {
	return vendorID == 0x1002 && ( deviceID == 0x163f || deviceID == 0x1435 );
}

// override, then Steam's variable, then the GPU (pass 0, 0 while it is unknown)
inline bool Sys_IsSteamDeckFrom( const char* overrideValue, const char* steamValue, unsigned int vendorID, unsigned int deviceID ) {
	const int forced = Sys_SteamDeckEnvValue( overrideValue );
	if ( forced >= 0 ) {
		return forced == 1;
	}
	if ( Sys_SteamDeckEnvValue( steamValue ) == 1 ) {
		return true;
	}
	return Sys_IsSteamDeckGpu( vendorID, deviceID );
}

inline bool Sys_IsSteamDeck( unsigned int vendorID = 0, unsigned int deviceID = 0 ) {
	return Sys_IsSteamDeckFrom( getenv( "OPENETQW_STEAMDECK" ), getenv( "SteamDeck" ), vendorID, deviceID );
}

/*
A fresh profile's display settings on a Deck, before the window opens: the panel's
native 1280x800 in fullscreen (gamescope scales anything else), vsync, and 60 fps,
which the LCD panel shows and which keeps the battery and the fan down (the OLED panel
goes to 90: set com_maxFPS 90). Ray-query lighting is off: the Deck's GPU has it, but
not the speed for it.
*/
static const char* const STEAMDECK_DISPLAY_DEFAULTS =
	"set r_mode -1\n"
	"set r_customWidth 1280\n"
	"set r_customHeight 800\n"
	"set r_fullscreen 1\n"
	"set r_swapInterval 1\n"
	"set com_maxFPS 60\n"
	"set r_rayTracing 0\n"
	"set r_multiSamples 0\n";

// the graphics preset for a fresh profile on a Deck (MachineSpec.h: 0 low, 1 normal, 2 high)
static const int STEAMDECK_MACHINE_SPEC = 1;

#endif /* !__SYS_STEAMDECK_H__ */
