// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __FRAMEWORK_GAMEPADBINDS_H__
#define __FRAMEWORK_GAMEPADBINDS_H__

/*
The default gamepad layout, after Destiny 2's default controller layout, for the first
pad (joy1; the key names are in src/sys/sdl/GamepadMapping.h). Applied when a profile
has no gamepad bindings at all (retail's defaultbinds.cfg binds none we know of), and by
the bindGamepadDefaults command. Start opens the menu (in_gamepadStartIsEscape).

The _zoomhold, _quickgrenade, _quickmelee, _reloaduse and _padsprint actions are the
game module's (GamepadControls.cpp). Banks 4 and 5 (class tool, deploy tool) follow
retail's number keys 5 and 6 and are still to be checked against retail's bindings.
*/
static const char* const GAMEPAD_DEFAULT_BINDS =
	"bind joy1_right_trigger \"_attack\"\n"			// fire
	"bind joy1_left_trigger \"_zoomhold\"\n"		// hold to zoom (in a vehicle: alt-fire)
	"bind joy1_1 \"_moveUp\"\n"						// A: jump
	"bind joy1_2 \"_moveDown\"\n"					// B: crouch (held)
	"bind joy1_3 \"_reloaduse\"\n"					// X: tap reload, hold use / enter / exit
	"bind joy1_4 \"_weapnext\"\n"					// Y: swap weapon
	"bind joy1_5 \"_quickgrenade\"\n"				// LB: grenade (hold to cook)
	"bind joy1_6 \"_weapon5\"\n"					// RB: deploy tool
	"bind joy1_7 \"_showScores\"\n"					// view/back: scoreboard
	"bind joy1_9 \"_padsprint\"\n"					// left stick click: sprint (boost in a vehicle)
	"bind joy1_10 \"_quickmelee\"\n"				// right stick click: melee
	"bind joy1_dpad_up \"_quickchat\"\n"			// quick chat
	"bind joy1_dpad_down \"_commandmap\"\n"			// command map
	"bind joy1_dpad_left \"_weapon4\"\n"			// class tool
	"bind joy1_dpad_right \"_taskmenu\"\n"			// missions
	"bind joy1_13 \"_ready\"\n"						// R4 (Steam Deck), Elite P1: ready up in warmup
	"bind joy1_14 \"_limbomenu\"\n"					// L4: team and class
	"bind joy1_15 \"_vehiclecamera\"\n"				// R5, Elite P2: vehicle camera
	"bind joy1_16 \"_prone\"\n"						// L5: prone
	"bind joy1_17 \"_fireteam\"\n";					// touchpad click: fireteam

#endif /* !__FRAMEWORK_GAMEPADBINDS_H__ */
