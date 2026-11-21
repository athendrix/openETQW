<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Gamepads and the Steam Deck

Status: built and unit-tested on Linux x64. **Not yet played with a real pad or on a
Deck**: the feel (speeds, curve, aim assist strength) still needs tuning by hand.

## Default pad layout

A profile with no pad bindings gets this layout, after Destiny 2's default controller
layout (`src/framework/GamepadBinds.h`). It uses only the buttons every Xbox-style pad
has: no back paddles, touchpad or gyro. `bindGamepadDefaults` binds it again.

| Button | On foot | With View held | In a vehicle |
|---|---|---|---|
| Right trigger | Fire | | Driver of a ground vehicle: throttle. Other seats: fire |
| Left trigger | Hold to aim (sights or scope) | | Driver: brake and reverse. Other seats: alt-fire |
| Left stick | Move | | Steer (driver) |
| Right stick | Look | | Look |
| Left stick click | Sprint, until the stick comes back | | Boost (held) |
| Right stick click | Prone (tap B to stand) | | Tophat controls |
| A | Jump | Vote yes | |
| B | Tap: crouch (stays down until B, jump or sprint). Hold: class ability (the deploy tool on and off; other classes hold their tool) | Vote no | Handbrake |
| X | Tap: reload. Hold: use, enter a vehicle, arm or disarm | Activate: switch item, launch decoys | Tap: switch seat or launch decoys. Hold: get out |
| Y | Tap: swap guns. Hold: heavy, the binoculars (Field Ops and Oppressor aim artillery with them) | Binoculars | Vehicle camera |
| LB | Grenade: hold to cook, release to throw, then back to your weapon | | Alt-fire |
| LB + RB | Super: the first item; held, the second (see below) | | |
| RB | Melee: one strike, then back to your weapon | | Fire |
| D-pad up | Quick chat | Context requests | |
| D-pad down | Command map | Vote menu | |
| D-pad left | Team and class | Fireteam | |
| D-pad right | Missions | Ready up | |
| View / Back | Scoreboard (held), and the second layer | | |
| Start / Menu | Menu (Escape) | | |

An empty cell means the button keeps its on-foot action. A ground-vehicle driver drives
Destiny Sparrow style (`in_vehicle_triggerThrottle 1`); `0` drives with the left stick.
Aircraft keep the stock stick controls, with the triggers firing.

In menus (whenever a mouse cursor shows), the left stick moves the cursor, A clicks,
B goes back, the right stick scrolls and the d-pad sends the arrow keys.

## Keyboard

A profile with no saved bindings gets Destiny 2's PC keys over retail's defaults
(`bindDestinyKeyboard` binds them again; a profile saved with an older version of this
layout gets the changed keys once, `com_destinyLayout`):

| Key | Action | Retail had |
|---|---|---|
| Q | Grenade: switches, cooks while held, throws on release, switches back | Lean left |
| E | Interact: use (held to arm or disarm), enter a vehicle | Lean right |
| C | Melee: switches, strikes once, switches back | Crouch |
| Ctrl | Crouch | Walk / handbrake |
| Alt | Walk / handbrake | |
| V | Class ability (B held): the deploy tool on and off; other classes hold their tool | Context requests |
| F | Super (LB + RB): a press uses the first item, a long press the second | Activate |
| 1, 2, 3 | Primary, sidearm, heavy (binoculars); the wheel swaps guns | Weapon banks |
| Shift | Sprint: a press runs until you stop moving forward | Sprint (held) |
| G | Context requests | Enter / exit vehicle |
| Right mouse | Hold to aim | Alt-fire (toggle) |
| X | Unbound (prone stays on Z) | Prone |

In vehicles Q and E strafe the Desecrator, F switches seat or launches decoys and G
gets out, as in retail. Lean on foot is left unbound. The rest is retail's.

Only the guns are ever "equipped". The super uses the class's items: a press the first
(Medic health pack, Field Ops ammo pack, Covert Ops third eye, Oppressor force shield,
Infiltrator flyer drone, Soldier and Aggressor charges, mines, Stroyent cells), a long
press the second (supply-crate marker, airstrike markers, smoke, teleporter). A placed
third eye: a press looks through it; while looking through it or flying the drone, a
press leaves the view (recalls the drone) and a long press detonates it. The class
ability brings the deploy tool out until you place something or press it again. Repair,
hack, arm, disarm and revive happen by using the thing (X held, or E).

Anything else retail puts in your hand by itself (the pliers when you use a damaged
vehicle, the hacking tool at an objective) goes back to your last gun a second after you
let go of fire and use.

The settings menu's Controls lists are this layout's actions (Grenade, Melee, Super,
Class ability, Heavy, Swap weapon, Interact, ...), so remapping changes which key is the
grenade key, as in Destiny; retail's weapon-bank rows are gone
(`src/game/decls/DestinyKeyBindings.h`). Tooltips that named a retail key ("press 4")
name the key of the action that does it now. On tools and items the aim button is
alt-fire (rotate a deployable, throw underhand).

The quick actions (grenade, melee, tool, item) leave you on your gun unless the key is
held. With `in_gunsOnly 1` (the default) the weapon keys, the wheel and next/previous
weapon only reach the sidearm and the primary; `seta in_gunsOnly 0` brings back
retail's selection, for the deploy tools and anything else that needs more than holding
fire. `in_quickDebug 1` prints what the quick actions do.

## Settings

Look (game module, `src/game/GamepadControls.cpp`):

| CVar | Default | |
|---|---|---|
| `in_aim_yawSpeed` / `in_aim_pitchSpeed` | 200 / 140 | Degrees a second at full stick |
| `in_aim_deadZone` / `in_aim_outerZone` | 0.1 / 0.03 | Radial dead zones |
| `in_aim_curve` | 2.2 | Response curve exponent (1 is linear) |
| `in_aim_rampDelay` / `in_aim_rampTime` / `in_aim_rampBoost` | 0.15 / 0.35 / 0.8 | Turn ramp at the stick's edge |
| `in_aim_zoomScale` | 1 | Look speed while zoomed, on top of the weapon's fov scaling |
| `in_aim_invertPitch` | 0 | Inverted look on the pad (also in vehicles) |
| `in_aim_lookX`, `in_aim_lookY`, `in_move_moveX`, `in_move_moveY` | 2, 3, 0, 1 | Axes; swap for southpaw |
| `in_aim_assist` | 1 | Aim assist on foot |
| `in_aim_assistFriction` / `in_aim_assistAdhesion` | 0.5 / 0.4 | Slow down on a target / share of its motion followed |
| `in_aim_assistRadius` / `in_aim_assistRange` | 40 / 4000 | Size of the assist area and the farthest target, in world units |
| `in_gyro_speed`, `in_gyro_invertPitch`, `in_gyro_zoomedOnly` | 1.5, 0, 0 | Gyro aiming (needs `in_gamepadGyro 1`) |
| `in_padHoldTime` | 250 | Milliseconds for X and Y to count as a hold (B prone: 100 more) |
| `in_vehicle_triggerThrottle` | 1 | Ground-vehicle drivers use the triggers (Sparrow style) |

Mouse look is inverted with the usual `m_pitch` (make it negative).

Engine (`src/sys/sdl/sdl_gamepad.cpp`): `in_gamepad`, `in_gamepadGyro`,
`in_gamepadTriggerPress` / `in_gamepadTriggerRelease`, `in_gamepadStartIsEscape`,
`in_gamepadMenuCursor`, `in_gamepadMenuCursorSpeed`, `in_gamepadMenuDeadZone`,
`in_gamepadLog`. `listControllers` lists the pads.

## Steam Deck

The Linux x64 build runs natively on the Deck.

1. Build the Linux package on a Linux PC (`./scripts/build-linux.sh --release`, then
   `tools/build/package_release.py` and `tools/build/package_linux_appimage.py`), or
   build it in the Deck's desktop mode.
2. Copy your retail 1.5 `base/` folder (with `pak000`-`pak008.pk4`) to the Deck, for
   example to `~/etqw/base`.
3. In desktop mode, add the AppImage to Steam as a non-Steam game, with the launch option
   `OPENETQW_BASEPATH=$HOME/etqw %command%`.
4. Controller: in the game's controller settings, either turn Steam Input off for it,
   which gives the game the Deck's back buttons and gyro directly, or keep Steam Input on
   with the "Gamepad" template. Steam Input then shows the game a plain Xbox pad: no back
   buttons and no gyro unless you map them in Steam.

On a fresh profile the game detects the Deck (Steam's `SteamDeck=1`, or the Deck's GPU)
and starts at 1280x800 fullscreen, vsync, 60 fps, the normal graphics preset, and ray
tracing off (`src/sys/SteamDeck.h`). On the OLED model, `com_maxFPS 90` uses its 90 Hz
panel. Text boxes and the console open Steam's on-screen keyboard. `OPENETQW_STEAMDECK=0`
or `1` overrides the detection.
