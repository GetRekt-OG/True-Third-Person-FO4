# True Third Person

An F4SE plugin for independent third-person movement and camera control in Fallout 4.

## Features

- Free camera orbit with a drawn weapon.
- Directional movement and smooth facing during hip fire.
- Melee target lock with automatic target handoff.
- Camera continuity through weapon changes, menus and window focus changes.
- Weapon models hidden while swimming, with visibility restored afterward.

## Requirements

- Fallout 4 **1.11.240.0** on Windows x64.
- F4SE matching that game version.
- Address Library data for that game version.

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
| `Main.Enabled` | `1` | Enable the plugin. |
| `Main.HideWeaponWhileSwimming` | `1` | Hide weapon scene objects during swimming. |
| `Main.HipFireFacing` | `1` | Face the firing direction during hip fire. |
| `Main.SmoothHipFire` | `1` | Blend facing into and out of hip fire. |
| `Main.KeepHolsterView` | `1` | Preserve the orbit after holstering. |
| `TargetLock.Enabled` | `1` | Enable melee target lock. |
| `TargetLock.Distance` | `2000` | Maximum target distance in game units. |
| `TargetLock.Response` | `12` | Target-facing and camera response. |
| `TargetLock.SwitchMousePixels` | `100` | Horizontal mouse travel required to switch targets. |

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

## Package contents

This source-only package intentionally omits build scripts, CMake configuration,
vendored dependencies, binaries and build output. Use the separate final-test
package to compile this revision with its existing dependency snapshots.
