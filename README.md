0.2.4.46 startup and sensitivity test: see [TEST-0.2.4.46.md](TEST-0.2.4.46.md).

Commonwealth Camera compatibility test 0.2.4.42.

# True Third Person 0.2.4.40 — OG compatibility test

Adds an OG 1.10.163.0 compatibility path to the confirmed 0.2.4.39 gameplay baseline.
This is a test source package; OG in-game behavior and Windows compilation are not verified here.
Read [FINAL-TEST.md](FINAL-TEST.md) before building. Install the complete Data folder.

# True Third Person

An F4SE plugin for independent third-person movement and camera control in Fallout 4.

## Features

- Free camera orbit with a drawn weapon.
- Directional movement and smooth facing during hip fire.
- Melee target lock with automatic target handoff.
- Camera continuity through weapon changes, menus and window focus changes.
- Weapon models hidden while swimming, with visibility restored afterward.

## Requirements

- Fallout 4 **1.10.163.0** (OG test), **1.11.221.0** or **1.11.240.0**, Windows x64.
- F4SE matching that game version.
- Runtime Database: `Data/F4SE/Plugins/f4rd-runtime.bin`. Address Library is no longer read by this plugin. Other installed mods may still require it.

## Controls

| Input | Behavior |
| --- | --- |
| Middle-click with melee drawn | Toggle target lock; native POV switching is blocked. |
| Mouse wheel while locked | Switch targets. |
| Horizontal mouse movement while locked | Switch targets after the configured movement threshold. |
| Mouse wheel while unlocked | Normal game zoom and POV controls. |
| V | Normal game POV switching. |

Target lock applies to melee weapons. Ranged weapons retain their normal targeting controls.

## Configuration

Settings are in `Data/F4SE/Plugins/TrueThirdPerson.ini`. Restart the game after changing them.

| Setting | Default | Purpose |
| --- | --- | --- |
| `Main.Enabled` | `1` | Enable the plugin’s independent-camera behavior. |
| `Main.HipFireHoldMs` | `3000` | Hip-fire facing hold in milliseconds (275–5000). |
| `TargetLock.Enabled` | `1` | Enable melee target lock. |
| `TargetLock.Distance` | `2000` | Maximum target distance in game units. |
| `TargetLock.Response` | `12` | Target-facing and camera response. |
| `TargetLock.SwitchMousePixels` | `100` | Horizontal mouse travel required to switch targets. |

The INI contains only these two Main and four TargetLock settings. Other features
use the 0.2.4.34 internal defaults. HipFireHoldMs=3000 gives a three-second hold;
4000 gives four seconds and 275 restores the old duration. Restart after changes.
Other legacy settings are ignored.

## Source layout

- `Main.cpp`: plugin entry points, camera hooks and directional input integration.
- `CameraSupport.hpp`: settings, hook installation and camera access.
- `MeleeLock.hpp`: target selection, input handling and lock-on camera updates.
- `EquipGuard.hpp`: favorite-input handling and input queue filtering.
- `WaterWeaponVisibility.hpp`: weapon scene visibility during swimming.
- `IdleRotation.hpp`: stationary character rotation filtering.
- `DirectionalMovement.hpp`, `FacingBlend.hpp`, `CombatMath.hpp`, `LockMath.hpp`,
  `EquipLogic.hpp`, `Logic.hpp`: movement, targeting and transition helpers.
- `StartupLog.hpp`: startup and diagnostic logging.
- `tests/`: portable C++ helper tests.

## Credits and licensing

See [CREDITS.md](CREDITS.md) for reference projects and dependencies.
The project license is in [LICENSE](LICENSE); third-party code retains its own terms.

## Build and install

1. Extract this package into a fresh folder.
2. Use the same Visual Studio C++ installation that built version 0.2.4.17.
3. Run `Build.cmd`.
4. Install the contents of `dist/Data` into the game's `Data` folder.
5. Launch through F4SE.

The script uses an x64 MSVC environment and CMake 4.3 or later. If CMake is missing,
it can install a project-local copy through Python. Dependencies are bundled.
`Build.log` and `Compiler.log` contain build output.

Load only one copy of the plugin; remove any old `ArmedOmniSprint.dll` installation.
Runtime diagnostics are written to `%TEMP%/TrueThirdPerson-startup.log`.
See [FINAL-TEST.md](FINAL-TEST.md) for the release checks.

## Changes from the supplied 0.2.4.34

- Keep independent movement active while wanting to holster or holstering.
  Ordinary draw-state eligibility remains exactly as in 0.2.4.34.
- Recognize PrimaryAttack as well as Attack for the confirmed bolt-action fix.
- Extend the existing firing-facing grace period from 275 ms to three seconds.
- Read only Main.Enabled, Main.HipFireHoldMs and the four requested target-lock options.

No later draw hooks, body-yaw restores, draw-frame diagnostics or runtime collectors
are included. Directional input, firing, target selection and weapon-state protection retain the
confirmed 0.2.4.39 behavior. Hook installation now selects the OG or AE layout.
NG 1.10.980/1.10.984 is not enabled. See OG-COMPATIBILITY.md for validation and limits.

## Commonwealth Camera compatibility

Version 0.2.4.42 avoids replacing Fallout 4's `ThirdPersonState::HandleLookInput` vtable entry. This is intentional for compatibility with Commonwealth Camera 1.5.x, whose OG build has its own look-input handling. TTP's independent orbit state is maintained from the camera-state update and rotation stages instead.


## Commonwealth Camera input-chain test 0.2.4.44

Restores TTP's `HandleLookInput` wrapper. The wrapper captures the existing vtable function first,
so it can preserve Commonwealth Camera's already-installed look-input path while allowing TTP to
update its independent orbit from mouse input.
