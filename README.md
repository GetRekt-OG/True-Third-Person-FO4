# True Third Person

F4SE plugin for Fallout 4 that makes the third-person camera independent of the
character. You can orbit freely with a weapon drawn, the character turns to face
the direction you move, and shooting or aiming turns them toward the camera.
Melee weapons get a target lock, and guns can have one too (off by default).

## Features

- Free camera orbit with a weapon drawn, holstered, or while switching weapons.
- Camera-relative movement: the body turns toward the direction you walk.
- Hip fire turns the upper body to the camera (torso twist); the legs keep your
  move direction.
- Grenades and mines go where the camera looks, holstered or drawn, standing or
  moving; from standing the upper body twists to throw.
- ADS waits for the body to finish turning so sights line up with the camera.
- Melee target lock with an on-screen marker, and an optional ranged lock.
- Lock keys can be changed; MCM has a reset-to-defaults switch.
- Separate mouse and controller sensitivity for holstered, drawn and ADS.
- Compass follows the camera, including in power armor.
- Weapon model hidden while swimming.
- Hands the camera back to the game in VATS and workbench menus, and picks up
  from the current view afterwards.

## Requirements

- Fallout 4 **1.10.163**, **1.10.980**, **1.10.984**, **1.11.191**, **1.11.221** or **1.11.240**
- F4SE for that game version

That's all. Addresses are built into the DLL, so Address Library isn't needed.
Other game versions are refused at load time, and Fallout 4 VR isn't supported.

## Install

Install with a mod manager, or copy the `Data` folder into the game folder.

## Controls

| Action | Mouse/keyboard | Controller |
| --- | --- | --- |
| Lock / unlock target | Middle mouse (changeable) | Right stick click (changeable) |
| Switch target | Mouse wheel, or a quick horizontal mouse flick | Flick right stick left/right |

Target lock works with a weapon drawn in third person. Melee and ranged weapons
each have their own settings and lock key; the ranged lock is off until you turn
it on. If the target dies, the lock moves to the nearest enemy. Losing line of
sight for more than about 0.6 seconds releases it.

Aiming down sights with a ranged lock pauses it: you aim freely, and the lock
picks the target up again when you lower the sights. The ranged lock keeps the
crosshair on the target, so hip fire goes where it points.

## Settings

Settings live in `Data/F4SE/Plugins/TrueThirdPerson.ini`:

```ini
[Main]
Enabled=1            ; 0 turns off camera/movement; target lock can still work
HipFireHoldMs=1500   ; how long the body keeps facing the camera after a shot
CustomRelaxTime=1    ; 0 = game's own timing for lowering the gun
RelaxTime=0.7        ; seconds the gun stays raised after a draw or shot

[TargetLock]
Enabled=1
ShowMarker=1         ; 0 hides the marker, lock still works
Distance=2000        ; max lock distance in game units
Response=12          ; how quickly the camera tracks the target
SwitchMousePixels=100
Key=0                ; 0 = middle mouse, see the INI for the full list
GamepadKey=0         ; 0 = right stick click

[RangedTargetLock]   ; same keys, for guns
Enabled=0
Distance=3000
```

`[CameraOffset]` moves the camera relative to the game's own shoulder position
(right, forward, up), separately for holstered, guns and melee. The values are
added to the game's `fOverShoulder*` settings rather than replacing them, so they
combine with other camera mods.

`[Controller]` and `[MouseKeyboard]` hold look sensitivity multipliers
(0.05–5.0) for holstered, drawn and ADS.

The INI is read at startup and again whenever you close the pause menu. All
settings, including `Enabled`, apply right away; no restart needed.

### Target lock without the TTP camera

With `Enabled=0` (or **Enable True Third Person** off in MCM) and target lock on,
you get lock-only mode: the normal game camera, plus the target lock. The character
turns to face the target (including up/down), and since the vanilla camera
follows the character with a weapon drawn, the view stays on the target. The
marker, switching and controls are the same.

To remove TTP completely, disable the plugin in your mod manager. With it
disabled in settings, its hooks stay installed but do nothing.

### MCM

If you use Mod Configuration Menu, open **Mod Config → True Third Person** and turn
on **Use MCM settings**. MCM then replaces the INI entirely; turning it off goes
back to the INI. MCM saves your choices to `Data/MCM/Settings/TrueThirdPerson.ini`,
which updates never overwrite. Changes apply when you close the pause menu.

**Reset all settings to defaults** on the main page puts every value back.
It's experimental: TTP has no Papyrus script, so it sets the values through MCM's
own functions. If the menu doesn't show the new values right away, close and
reopen it. Your INI isn't touched.

## Compatibility

- **Fallout 76 VATS**: movement and camera go back to the game while VATS is open.
- **Toggle Aim**: detected automatically. ADS then uses native timing, so the ADS
  turn is skipped. Hip-fire facing still works.
- **Reload Fix**: detected automatically. POV switching keeps the orbit.
- **Commonwealth Camera**: look input is chained, not replaced.
- **Full Body First Person**: both hook the compass; TTP chains onto it.
- **Wheel Menu Remastered** and other favorites wheels: the camera keeps its
  orbit while the wheel is open, and a weapon picked from it switches like a
  favorites key.

## Troubleshooting

TTP writes a short startup log to `%TEMP%\TrueThirdPerson-startup.log`. When
reporting a problem, include that file, `f4se.log`, your game version, and what
you were doing when it went wrong. A crash log helps if you have one.

For FPS questions, set `[Debug] Profile=1` in the INI. Every 5 seconds the same
log gets a line with TTP's own time per frame and the game's frame time.

## Building

Windows, Visual Studio 2022 17.14+ (or 2026) with **Desktop development with
C++**, and CMake 4.3+. All C++ dependencies are in `external` (CommonLibF4 is a
modified copy with TTP's built-in address tables).

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix dist
```

The finished mod is in `dist/Data`. The HUD marker SWF is prebuilt in
`package/Data/Interface`; its source is `HUD/LockMarker.as`.

### Tests

The engine-free logic and the address tables have tests that run on any OS:

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

In-game checks are listed in [TESTING.md](TESTING.md).

### Source layout

| File | What it does |
| --- | --- |
| `src/Main.cpp` | Plugin entry, player update, movement hooks, setup |
| `src/CameraOrbit.hpp` | The orbit itself: transitions, hip fire, ADS, loading, VATS |
| `src/NativeHooks.hpp` | Power armor camera and compass call-site patches |
| `src/TargetLock.hpp`, `src/LockMarker.hpp` | Target lock (melee and ranged) and its marker |
| `src/TorsoTwist.hpp` | Hip-fire and throw twist (spine and hip bones) |
| `src/McmReset.hpp` | MCM reset-to-defaults switch |
| `src/ADSStateHook.hpp` | Delays ADS until the body has turned |
| `src/Input.hpp` | Input device tracking and the favorites equip guard |
| `src/CameraSupport.hpp` | Settings and shared game-state helpers |
| `src/WaterWeaponVisibility.hpp` | Hides the weapon while swimming |
| `src/Logic.hpp` | Engine-free math, covered by `tests/logic.cpp` |
| `external/CommonLibF4/.../BuiltinAddresses.hpp` | Per-version address tables |

See [RUNTIME-SUPPORT.md](RUNTIME-SUPPORT.md) for adding a new game version.

## Credits

See [CREDITS.md](CREDITS.md). The project is MIT licensed (`LICENSE`); third-party
licenses are kept in `THIRD-PARTY` and with each dependency.
