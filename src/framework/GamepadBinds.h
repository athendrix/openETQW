// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __FRAMEWORK_GAMEPADBINDS_H__
#define __FRAMEWORK_GAMEPADBINDS_H__

/*
Default bindings after Destiny 2's, for the first pad and for the keyboard.

The pad layout uses only the buttons every Xbox-style pad has (no back paddles, touchpad
or gyro). The view/back button is also a modifier: held, it gives the face buttons and the
d-pad a second layer. The "vehicle" bind context (retail's g_bind_context_<vehicle>, in
defaultbinds.cfg) changes a few buttons in every vehicle seat; anything it leaves unbound
falls back to the default context (idGameLocal::Translate). The pad's key names are in
src/sys/sdl/GamepadMapping.h; Start opens the menu (in_gamepadStartIsEscape).

The layout is Destiny 2's controller defaults (LB grenade, RB melee, LB + RB super, B
crouch and held class ability, Y swap and held heavy, X reload and held interact), and
the keyboard follows it (Q, C, F, V, 1-3). The bank numbers are retail's
(defaultbinds.cfg): _weapon0 melee, 1 one-handed, 2 two-handed, 3 grenades, 4 tools,
5 items, 6 binoculars; with in_gunsOnly only 1 and 2 are selected by a key. The underscore actions not in retail are the
game module's (GamepadControls.cpp).

A profile with no pad bindings gets GAMEPAD_DEFAULT_BINDS (bindGamepadDefaults binds it
again); a profile with no saved binding file gets DESTINY_KEYBOARD_BINDS on top of
retail's keyboard defaults (bindDestinyKeyboard binds it again).
*/
static const char* const GAMEPAD_DEFAULT_BINDS =
	// earlier drafts bound the back paddles and the touchpad
	"unbind joy1_13\n"
	"unbind joy1_14\n"
	"unbind joy1_15\n"
	"unbind joy1_16\n"
	"unbind joy1_17\n"

	// on foot
	"bind joy1_right_trigger \"_padfire\"\n"		// fire
	"bind joy1_left_trigger \"_zoomhold\"\n"		// hold to aim down sights or scope
	"bind joy1_1 \"_moveUp\"\n"						// A: jump
	"bind joy1_2 \"_crouchprone\"\n"				// B: tap crouch, hold the class ability (deploy tool)
	"bind joy1_3 \"_reloaduse\"\n"					// X: tap reload, hold use / enter vehicle
	"bind joy1_4 \"_swapweapon\"\n"					// Y: tap swap guns, hold the heavy (binoculars, artillery)
	"bind joy1_5 \"_quickgrenade\"\n"				// LB: grenade, hold to cook; LB + RB: super (the item)
	"bind joy1_6 \"_quickmelee\"\n"				// RB: melee; LB + RB: super, held for the second item
	"bind joy1_7 \"_showScores\"\n"					// view: scoreboard, and the second layer
	"bind joy1_9 \"_padsprint\"\n"					// left stick click: sprint
	"bind joy1_10 \"_prone\"\n"					// right stick click: prone (Destiny has none)
	"bind joy1_dpad_up \"_quickchat\"\n"			// quick chat
	"bind joy1_dpad_down \"_commandmap\"\n"			// command map
	"bind joy1_dpad_left \"_limbomenu\"\n"			// team and class
	"bind joy1_dpad_right \"_taskmenu\"\n"			// missions

	// with view held
	"bind joy1_1 \"vote y\" joy1_7\n"				// vote yes
	"bind joy1_2 \"vote n\" joy1_7\n"				// vote no
	"bind joy1_3 \"_activate\" joy1_7\n"			// switch item, launch decoys
	"bind joy1_4 \"useWeapon weapon_binocs\" joy1_7\n"	// binoculars / targeting tool
	"bind joy1_dpad_up \"_context\" joy1_7\n"		// context requests (need a medic, ...)
	"bind joy1_dpad_down \"_votemenu\" joy1_7\n"	// call a vote
	"bind joy1_dpad_left \"_fireteam\" joy1_7\n"	// fireteam
	"bind joy1_dpad_right \"_ready\" joy1_7\n"		// ready up in warmup

	// in any vehicle seat. A driver of a ground vehicle drives with the triggers (right
	// throttle, left brake and reverse; in_vehicle_triggerThrottle), so the vehicle's guns
	// are on the bumpers; in other seats the triggers fire and alt-fire as on foot
	"bind joy1_6 \"_attack\" \"\" vehicle\n"			// RB: fire
	"bind joy1_5 \"_altattack\" \"\" vehicle\n"		// LB: alt-fire
	"bind joy1_2 \"_speed\" \"\" vehicle\n"			// B: handbrake
	"bind joy1_4 \"_vehicleCamera\" \"\" vehicle\n"	// Y: camera
	"bind joy1_10 \"_tophat\" \"\" vehicle\n";		// right stick click: tophat controls
	// X is still _reloaduse: in a vehicle a tap switches seat or launches decoys, a hold gets out

static const char* const DESTINY_KEYBOARD_BINDS =
	"bind CTRL \"_movedown\"\n"						// crouch
	"bind c \"_quickmelee\"\n"						// melee (RB)
	"bind q \"_quickgrenade\"\n"					// grenade, hold to cook (LB)
	"bind e \"_interact\"\n"						// interact: use, enter; in a vehicle tap switches seat, hold gets out
	"bind v \"_quicktool\"\n"						// class ability (B held): the deploy tool on and off, else the tool while held
	"bind f \"_quickitem\"\n"						// super (LB + RB): a press the first item, a long press the second
	"bind 1 \"_weapon2\"\n"						// the primary
	"bind 2 \"_weapon1\"\n"						// the sidearm
	"bind 3 \"_heavy\"\n"							// the heavy: binoculars (Y held)
	"unbind 4\n"
	"unbind 5\n"
	"unbind 6\n"
	"unbind 7\n"
	"bind SHIFT \"_padsprint\"\n"					// sprint: a press runs until you stop moving forward
	"bind MOUSE2 \"_zoomhold\"\n"					// hold to aim
	"bind ALT \"_speed\"\n"							// walk, vehicle handbrake (retail Ctrl)
	"bind g \"_context\"\n"							// context requests (retail V)
	"unbind x\n"									// prone stays on z
	"unbind F11\n"									// retail screenshot: use the OS's screenshot tool
	"unbind F12\n"									// retail toggleNetDemo: no demo recording
	// in vehicles the keys Destiny took back are retail's again: Q and E strafe the
	// Desecrator (lean), F switches seat or launches decoys, G gets out (E still gets in)
	"bind q \"_leanleft\" \"\" vehicle\n"
	"bind e \"_leanright\" \"\" vehicle\n"
	"bind f \"_activate\" \"\" vehicle\n"
	"bind g \"_usevehicle\" \"\" vehicle\n";
	// lean on foot (retail q and e) is left unbound

#endif /* !__FRAMEWORK_GAMEPADBINDS_H__ */
