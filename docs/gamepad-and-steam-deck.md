<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Gamepads and the Steam Deck

Status: built and unit-tested on Linux x64. **Not yet played with a real pad or on a
Deck**: the feel (speeds, curve, aim assist strength) still needs tuning by hand.

## Default pad layout

A profile with no pad bindings gets this layout, after Destiny 2's default controller
layout (`src/framework/GamepadBinds.h`). `bindGamepadDefaults` binds it again.

| Button | Action |
|---|---|
| Right trigger | Fire |
| Left trigger | Hold to zoom (sights or scope); in a vehicle, alt-fire |
| Left stick / right stick | Move / look |
| Left stick click | Sprint, until the stick comes back (boost in a vehicle) |
| Right stick click | Melee (one swing, then back to your weapon) |
| A / B | Jump / crouch (hold) |
| X | Tap: reload. Hold: use, enter or leave a vehicle, arm or disarm |
| Y | Next weapon |
| LB | Grenade: hold to cook, release to throw, then back to your weapon |
| RB | Deploy tool (`_weapon5`) |
| D-pad | Up: quick chat. Down: command map. Left: class tool (`_weapon4`). Right: missions |
| View / Back | Scoreboard |
| Start / Menu | Menu (Escape) |
| Steam Deck R4 / L4 / R5 / L5 | Ready up / team and class / vehicle camera / prone |
| Touchpad click | Fireteam |

In menus (whenever a mouse cursor shows), the left stick moves the cursor, A clicks,
B goes back, the right stick scrolls and the d-pad sends the arrow keys.

The new bindable actions also work on a keyboard or mouse, for example
`bind MOUSE2 _zoomhold` and `bind g _quickgrenade`.

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
| `in_padHoldTime` | 250 | Milliseconds for X to count as a hold |

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
   `ETQW_DATA_PATH=$HOME/etqw %command%`.
4. Controller: in the game's controller settings, either turn Steam Input off for it,
   which gives the game the Deck's back buttons and gyro directly, or keep Steam Input on
   with the "Gamepad" template. Steam Input then shows the game a plain Xbox pad: no back
   buttons and no gyro unless you map them in Steam.

On a fresh profile the game detects the Deck (Steam's `SteamDeck=1`, or the Deck's GPU)
and starts at 1280x800 fullscreen, vsync, 60 fps, the normal graphics preset, and ray
tracing off (`src/sys/SteamDeck.h`). On the OLED model, `com_maxFPS 90` uses its 90 Hz
panel. Text boxes and the console open Steam's on-screen keyboard. `OPENETQW_STEAMDECK=0`
or `1` overrides the detection.
