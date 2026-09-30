# Changelog

## Unreleased

- Grenades and mines go where the camera looks. The game throws them the way
  the hand holds them, which with the free camera meant the way you last walked
  (holstered), or for mines off to the side: about 90 degrees left from a
  relaxed gun, 30 degrees right from a ready one, 20 degrees right holstered.
  The first frame after release, the player's grenade or mine gets its physics
  velocity set again, turned to land on the crosshair line at the distance the
  throw carries (pitch kept), and again for three frames with gravity allowed
  for. It goes through the game's own velocity setter, the one
  Projectile::AddInitialVelocity calls; that has no table address and is found
  at run time from the grenade class's vtable (slot 0xE7 and the base version
  it calls), recognised by the bytes after each call (checked on OG and AE).
  Not found: no correction, logged. Each throw logs "Throw: projectile turned
  N degrees to the crosshair".
- Throwing from standing (the Melee key held, drawn or holstered) turns the
  actor to the camera with the legs planted up to the torso-twist limit: the
  lower body (COM, or Pelvis) is turned back and the spine turns the upper body
  forward, like the hip-fire twist. The legs turn in place to catch up after.
  Throwing while moving keeps you moving where you steer, with the spine
  twisted to the camera. A tap (weapon bash) also goes where you look.
- The throw key counts as up when its per-frame events stop for 0.3 s, so a
  release the game never sees (Alt-Tab: Melee is Alt) can't leave the body
  held to the camera.

## 0.2.5

- Hip-fire torso twist, reworked (MCM **Hip-fire torso twist**, INI
  `TorsoTwistDegrees`, default 60). Instead of the whole body snapping to the
  camera when you hip fire, the legs keep your move direction (or stay where you
  stand) and the spine turns the upper body, arms and gun toward the crosshair.
  The body itself only turns for what is past the limit. The spine bones
  (SPINE1/SPINE2/Chest) are turned right after the player's animation update
  (TESObjectREFR::UpdateAnimation, vtable slot 0x9F, checked on OG and AE), so it
  works with any animation set, and the twist eases in and out. While twisted,
  the keys are given to the game relative to the body, so movement stays on
  the keys. 0 brings back the full-body turn. The spine is turned about true
  vertical: the engine composes bone rotations as local * parent (used from the
  first frame; still checked on the skeleton at run time and logged), and
  parent rotations are rebuilt from this frame's pose. Switching sides eases
  the legs round (about a third of a second for left to right) while the twist
  tracks the exact gap, so the chest and gun stay on the crosshair during the
  swing.
- New option **Only when the gun is relaxed** (MCM main page, INI
  `OnlyWhenRelaxed`, off by default). With a gun out and ready, the game's own
  camera and movement are used; True Third Person takes over when the gun drops
  to the relaxed pose, picking the orbit up from the current view. When the gun
  comes up again, the body turns to the current view first so the picture
  doesn't jump. Holstered and melee are unchanged; a target lock is kept across
  the switch.
- New: camera offsets (MCM **Camera Offsets** page, INI `[CameraOffset]`):
  side/height for holstered, side/distance/height for guns and for melee. They
  are added to whatever the game's `fOverShoulder*` settings hold when the game
  picks the offset, so they stack with the INI and with camera mods that write
  those settings (Comprehensive 3rd Person Camera Tweaks, Custom Camera 0.4 and
  0.5.5, Commonwealth Camera): +40 here and -40 from another mod give 0.
- Fixed: after aiming down sights, the camera kept the holstered shoulder offset
  instead of the drawn one (reported with Custom Camera on OG, but every version
  was affected). Leaving ADS makes the game pick the offset again, and inside
  TTP's camera update it read the weapon as holstered.
- Fixed: moving back and to the side (e.g. S + D) around hip fire ran the
  character the wrong way (DrawTrace showed movement 180 degrees off the keys).
  Two causes: once the gun relaxed during the hip-fire hold, the game left combat
  stance and stopped strafing; and when independent movement took over again,
  the character ran forward along the body while it turned. Hip fire now keeps
  combat stance for its whole hold, and during the handover (up to 1 s, until the
  body lines up with the keys) the keys are given relative to the body in combat
  stance, so the character strafes where the keys point while it turns. The same
  handover runs after a draw, ADS and a target lock.
  A relaxed gun puts the game into its relaxed locomotion, which turns and runs
  (with pivot animations) instead of strafing, so: the gun is kept ready while
  TTP holds or strafes the body (the short relax time applies again afterwards),
  the hip-fire hold ends if the gun relaxes anyway, and the strafe handover isn't
  used with a relaxed gun.
- Stability review fixes:
  - The lock marker's screen position is worked out on the main thread; the
    marker menu (which may advance on a UI thread) only reads it.
  - The equipped-weapon scan holds the inventory's read lock.
  - A missing LockMarker.swf no longer leaves an empty menu being opened on
    every lock.
  - Compass: each heading call keeps its own original, so a mod that hooked only
    one of them (Full Body First Person) keeps its hook at that site. Pivot calls
    already changed by another mod are left alone.
  - A lock release no longer stores a NaN pitch.
  - Patch trampolines and swimming-hidden weapon nodes are never freed, so game
    shutdown can't run through freed memory.
  - Log writes use small buffers.
- `DrawTrace` also records movement handovers after hip fire, ADS and locks.
- New: custom weapon relax time (MCM **Custom weapon relax time** / **Relax after
  (seconds)**, INI `CustomRelaxTime` / `RelaxTime`). Sets the game setting
  `fGunPlayerRelaxedWaitTime`, so the gun drops to the relaxed pose right after
  the draw. Turning it off restores the game's own value.
- Defaults: hip-fire facing hold 1500 ms (was 3000), relax time 0.7 s.
- Fixed: drawing a weapon while moving in any direction with a sideways part
  (left, right, diagonals, back) slid the character about 35 degrees off the
  stick direction for the whole draw, then curved back over 0.25 s (measured with
  DrawTrace; straight forward was fine). Mid-draw the stick is now turned against
  that offset. The offset is learned per stick direction while playing, starting
  from the measured 35 degrees.
- Fixed: a 1-2.5 ms hitch whenever TTP wrote a line to its startup log during
  play (every ADS entry writes two). Each line was forced to disk; it is now
  only flushed, which still survives a game crash. The log also moves to
  `TrueThirdPerson-startup.old.log` once it passes 1 MB instead of growing forever.
- `[Debug] DrawTrace=1` logs every frame of the first 1.5 s of each weapon
  draw: stick input, TTP's input mode, body/camera/wanted/actual movement yaw,
  speed and the sideways drift, plus the largest drift. For tracking down the
  remaining sideways slide on draw.
- Fixed: the profiler counted game time twice when TTP hooks were nested (the
  camera update inside the player update), so `[Debug] Profile=1` reported
  negative averages.
- Ranged target lock for guns, with its own MCM page and `[RangedTargetLock]`
  INI section. Off by default; default distance 3000 (melee 2000). Same controls
  as the melee lock. Aiming down sights pauses it (you aim freely, the lock is
  kept) and it resumes when you lower the sights.
- Lock keys are configurable per lock type: a mouse/keyboard key (middle mouse,
  mouse 4/5 or a letter the game doesn't use) and a controller button. Defaults
  are unchanged (middle mouse, right stick click). `None` disables a key.
- MCM: **Reset all settings to defaults** on the main page (experimental). TTP
  writes the defaults through MCM's Papyrus functions and to MCM's settings file;
  no script is involved. **Use MCM settings** is left on.
- `MeleeLock.hpp` is now `TargetLock.hpp`.
- Ranged lock aims the crosshair, not just the body: with TTP on, the actor
  carries the view pitch so the gun points at the target, and the view tracks
  2.5x tighter than the Response setting. In lock-only mode the character turns
  until the crosshair (not the body) is on the target.
- Wheel menus (e.g. Wheel Menu Remastered): mod menus that take menu context
  but leave the third-person camera running no longer stop the orbit. Weapon
  switches that don't come from a favorites key (wheel, Pip-Boy, scripts) now
  get the same camera handling as a favorites switch. If a game menu does stop
  the orbit, it restarts from what is on screen when the menu closes.
- Performance: the "is a melee weapon equipped" check scanned the whole inventory
  and ran on every input event in two input hooks, plus in several camera hooks,
  so mouse movement (e.g. while aiming) multiplied it. It is now cached for 20 ms.
  Window focus, VATS, pause and workbench menu checks are cached for 4 ms.
- `[Debug] Profile=1` in the INI writes TTP's own per-frame cost (average and
  worst, excluding game code it calls) plus the game's frame time to the startup
  log every 5 seconds.
- Fixed: drawing a weapon turned the body to the camera for the draw, then
  snapped back to the movement direction. The draw states (`kWantToDraw`,
  `kDrawing`) were missing from independent movement and the idle-turn blocker.
  During a normal draw the stick input is no longer remapped, since the game
  still moves camera-relative mid-draw (the remap made the character drift sideways).
  Right after the draw, movement stays camera-relative until the body faces the
  movement direction (max 0.5 s), which removes the last bit of drift.
- Fixed: switching favorites from a melee weapon while sprinting kept the old
  melee sprint pose with empty hands until the sprint ended. Sprint is now paused
  for the length of a switch from melee, so it behaves like a switch while running.
  Afterwards sprint resumes on its own if the sprint key is still held (or
  toggle-sprint was on) and you're still moving.
- The main switch (`Enabled`, MCM **Enable True Third Person**) now applies when
  the pause menu closes; no restart. Hooks are always installed and pass straight
  through while disabled. Switching on picks up the orbit from the current view.
- Lock-only mode: melee target lock works with the main switch off. The character
  turns (yaw and pitch) to the target and the vanilla camera follows.
- Fixed: shoulder offsets from `fOverShoulderCombat*` and
  `fOverShoulderMeleeCombat*` (INI or other camera mods) were ignored with a
  weapon drawn. TTP called the camera's weapon-drawn callback with "holstered"
  whenever it owned the orbit, and that callback is where the game picks the
  shoulder offset. It now passes the real weapon state; the orbit is still restored.
  Checked in the OG 1.10.163 and AE 1.11.191 executables: the callback only sets
  `targetShoulderOffset` from those settings and touches nothing else.
- Fixed (likely): mouse pointer disappearing in crafting menus after crafting an
  item. The lock marker overlay used to stay on the menu stack for the whole
  session. It now only opens while a target is locked in normal gameplay and
  closes as soon as any cursor/menu-mode menu opens, so it never shares the
  stack with a workbench menu.
- Removed three workbench menu names that don't exist in the game
  (`ChemLabMenu`, `WeaponModMenu`, `ArmorModMenu`). Chem and cooking stations use
  `CookingMenu`; weapon and armor benches use `ExamineMenu`. Both were already listed.
- Menu open/close events around workbenches are written to the startup log.
- Added Fallout 4 1.11.191.
- Fixed: the legacy F4SE entry points had their own version lists, which didn't
  include 1.11.191, so the plugin would still refuse to load there. All three
  now use the same list.
- Cleanup, no behavior changes intended:
  - Merged the three orbit-owning camera hooks into one shared body, and the
    three "resume from current view" paths (VATS, workbench, loading) into one.
  - Camera state is read through typed members instead of raw `offsetof` pointer
    math (also removes an undefined-behavior warning).
  - Moved the compass and power armor patches into `NativeHooks.hpp` with shared
    validation and patching helpers.
  - Folded eleven small logic headers into `Logic.hpp` and the tests into two files.
  - Removed never-set spoof flags left over from an earlier movement approach.
  - Removed per-action debug traces (hip fire, holster, POV, idle rotation);
    state changes and hook installation are still logged.
  - Dropped fmt/spdlog tests, docs, benchmarks and CI files from `external`.
  - Rewrote the README; in-game checks moved to `TESTING.md`.

## 0.2.4.59

- Optional MCM menu for general, target lock, controller and mouse settings.
- Settings are re-read when the pause menu closes. The master switch still
  needs a restart.
- New `ShowMarker` INI option to hide the lock marker without disabling the lock.
- Crafting, chem, armor and weapon mod menus now suspend TTP like other workbenches.

## 0.2.4.58

- VATS gets the camera and player facing while it's open; TTP picks up from the
  current view when it closes.
- Target lock and pending ADS/hip-fire turns are cleared on entering VATS.

## 0.2.4.57

- OG: saving works while the lock marker overlay is open.

## 0.2.4.56

- Toggle Aim is detected and ADS is left synchronous when it's installed.
  Hip-fire smoothing is unaffected.

## 0.2.4.55

- Target lock and camera orbit survive opening the pause menu.

## 0.2.4.54

- Movement, stance and rotation pass through untouched while VATS is open, so
  Fallout 76 VATS keeps strafing and backward movement.

## Earlier

- Inventory items are checked by form type before being treated as weapons
  (fixed a missing-RTTI crash on OG).
- OG lock marker uses fewer menu flags to avoid losing game audio.
- No forced position updates during hip fire, so jumping stays native.
- 0.2.4.53 added the constructor vtables missing from 0.2.4.52, including
  UIMessage, which crashed after loading a save.
