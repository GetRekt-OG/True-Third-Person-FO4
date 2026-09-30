# In-game test checklist

Run these after changes to hooks or camera code. Restart the game after replacing
the DLL, and check `%TEMP%\TrueThirdPerson-startup.log` for any "FAILED" lines.

## Basics

- Draw, holster, move, turn, fire and switch favorites in third person.
- Compass stays aligned with the camera, weapon drawn and holstered, in and out of
  power armor.
- Controller: look sensitivity per state and smooth movement direction.
- Swimming hides the weapon; getting out restores it.

## Camera offsets

- Set clearly different values for `fOverShoulderPosX`, `fOverShoulderCombatPosX`
  and `fOverShoulderMeleeCombatPosX` under `[Camera]` in `Fallout4Custom.ini`.
  Holstered, gun drawn and melee drawn should each use their own offset, and
  switching between them should blend without the orbit jumping. Check on OG too.

## Main switch and lock-only mode

- In MCM, turn True Third Person off and close the pause menu, in third person
  with a weapon drawn and holstered. The camera should return to vanilla behavior
  with no stuck orbit. Turn it back on: orbit resumes from the current view.
- With TTP off, check that vanilla camera settings, ADS, VATS, workbenches and
  power armor behave like TTP isn't installed (apart from target lock).
- With TTP off and target lock on, draw a melee weapon, lock, walk around the
  target, and lock onto an enemy above/below you. The view should stay on the
  target. Switch targets, unlock, holster.
- Toggle the main switch while a target is locked; the lock releases cleanly.

## Draw and favorites

- Walk and run in different directions with the camera to the side, then draw
  (R). The body should keep facing the movement direction, with no turn to the
  camera and no snap back after the draw. Repeat standing still.
- Draw while moving in each direction: no sideways drift during the draw.
- While sprinting, switch favorites: gun→gun, gun→melee, melee→melee and
  melee→gun. Switches from melee should drop to a run for the switch, like
  switching while running, with no empty-handed melee pose. The startup log
  shows "sprint paused for a switch from melee".
- Keep holding sprint through a melee switch: sprint should resume by itself
  ("sprint resumed after the switch" in the log). Release sprint mid-switch:
  it should not resume. Repeat with toggle-sprint.

## Performance

- Set `[Debug] Profile=1` in the INI and play normally, including ADS in heavy
  areas. The startup log gets a "Profile:" line every 5 seconds. TTP's cost
  should be a few tens of microseconds per frame; a game frame at 60 fps is
  16,700 us. Set it back to 0 afterwards.
- In a heavy area, aim down sights and sweep the view slowly. Note the FPS, then
  turn True Third Person off in MCM and repeat the same sweep. A large difference
  points at TTP; the same drop both ways is rendering (scene, ENB, shadows).
- Switch between melee and guns quickly; each weapon type must use its own lock
  settings, and the ranged lock must not engage while it is off.

## Target lock

- Lock, switch with wheel / mouse flick / stick flick, unlock.
- Kill the target: the lock should move to the nearest enemy or release cleanly.
- Lock a target, press Esc, wait a few seconds, close the menu. Lock and view
  should be unchanged. Repeat from a settings submenu and in power armor.
- Change the melee lock key in MCM (e.g. to Mouse 4 and to a letter key), close
  the menu, and lock with the new key. Middle mouse should now reach the game.
  Repeat for the controller button. Set a key to None: that key no longer locks.
- Ranged lock: turn it on in MCM, draw a gun, lock an enemy at long range.
  Hip-fire at it while strafing. Aim down sights: the lock pauses and you can aim
  freely (marker stays). Lower the sights: the camera goes back to the target.
  Kill the target while in ADS, lower the sights: the lock moves to the nearest
  enemy or releases.
- Lock with a gun, switch to a melee weapon via favorites: the lock releases and
  the melee lock uses the melee key and distance.
- Lock-only mode (TTP off) with a gun: the crosshair (not just the body) sits on
  the target, including targets above/below.
- Ranged lock with TTP on: strafe around the target; the crosshair stays on it
  and hip fire hits. Try a target on a rooftop.

## Favorites wheel (Wheel Menu Remastered)

- With a weapon drawn, open the wheel, pick another weapon, repeat for melee to
  gun, gun to gun, gun to melee, and with bullet-time on and off. The view must
  not jump or flip around. The startup log shows "Favorites: weapon changed
  outside the hotkeys; camera held".
- Open and close the wheel without picking anything: no camera change.
- Equip a weapon from the Pip-Boy with one drawn: no jump after closing it.


## ADS

- Aim from a camera angle well off the character's facing; the body turns first,
  then the sights come up.
- With Toggle Aim: tap aim, release, tap again to leave ADS. Repeat in first
  person, in power armor, and after loading a save. The startup log should show
  "Toggle Aim detected".

## VATS (Fallout 76 VATS)

- With a target selected, move forward, back and sideways on keyboard and controller.
- The camera tracks the target while moving. Switch targets and fire.
- Exit VATS while moving: no camera jump, independent movement resumes.
- Repeat in power armor, with a melee lock active beforehand, and in first person.

## Menus and saving

- Open and close a workbench, Pip-Boy and other menus; the camera continues from
  the current view.
- Craft several items in a row at a chem station and a weapons bench using the
  mouse. The pointer must stay visible, before and after each craft. Repeat after
  having a melee target locked just before entering.
- Lock marker: appears when you lock, disappears on unlock, hides while the
  Pip-Boy or pause menu is open and comes back after.
- OG (1.10.163): manual save and quicksave after loading, after releasing a
  lock, and after leaving ADS. Game audio should keep working.
- Switch POV with Reload Fix installed; the orbit should be kept.

## MCM

- Enable the MCM profile, lower a sensitivity, close the pause menu, check it applied.
- With a lock active, hide the marker in MCM and close the menu: targeting stays,
  marker is gone. Show it again; no camera jump.
- Change several values on every page, then turn on **Reset all settings to
  defaults**. Within about a second the switch turns off and the values show
  their defaults (or after reopening the menu). Close the menu; the startup log
  shows "MCM reset to defaults: N values written, N sent to MCM". **Use MCM
  settings** stays on.
- Restart and check the settings persisted. Disable the MCM profile and check the
  INI values are back.

## Draw drift

- Holstered, run in each of the 8 directions (forward, back, left, right,
  diagonals) along a straight road line and draw mid-run. The character must
  keep following the line through the draw, with no curve after it.
- With `[Debug] DrawTrace=1` the `side` column should stay within a few degrees
  during `Drawing` frames (the first draw in each direction may take ~0.1 s to settle).

## Relax time

- Draw a gun with the default settings: it goes to the relaxed pose right after
  the draw. Shoot: it raises for the shot and relaxes again right after.
- Set 3 seconds in MCM, close the menu: the gun stays raised about 3 s.
- Turn the switch off: the game's own timing is back. The startup log shows
  "Relax time: ..." with the game's value.
- Melee weapons and power armor behave the same way.

## Hip fire release

- Hip fire, then keep S + D (and every other direction) held until the hip-fire
  facing ends. The character must keep moving where the keys point while the
  body turns, with no swing toward the camera. With `DrawTrace=1` the log gets a
  "movement resumed" block whose `side` column stays small.

## Camera offsets

- Set gun Side to 40 in MCM, close the menu: the camera moves right with a gun
  drawn, not holstered. Holster, draw, aim and stop aiming: the offset stays.
- With a camera mod that sets fOverShoulderCombatPosX to -40 (Custom Camera, CTPC
  INI), the same +40 gives a centered camera.
- Custom Camera profile with centered unarmed and left-shoulder armed: draw, aim,
  stop aiming. The armed (left) position must come back after aiming.

## Only when relaxed

- Turn it on. Draw a gun: game camera (body faces the view). After the relax
  time the gun lowers and the orbit is free again, from the same view.
- Shoot from relaxed, aim, reload: the game camera each time, no view jump; then
  back to TTP after the relax time. The log shows "Gun ready" / "Gun relaxed".
- Lock a target, shoot and let it relax: the lock stays.
- Melee and holstered behave as with the option off.

## Hip-fire torso twist

- Stand still, look 45 degrees to the side and hip fire: the legs stay, the upper
  body and gun turn to the crosshair. Look 120 degrees away: the body turns until
  the twist is within the limit.
- Run in each direction while hip firing sideways and backwards: legs run the
  move direction, torso aims at the crosshair, movement follows the keys.
- Shots must land on the crosshair (the log line "Torso twist: 3 of 3 spine bones
  found" shows the bones were found). If shots go where the legs point, report it.
- After the hold ends the torso untwists smoothly. Set 0: the old full-body turn.
- Power armor: unchanged (full-body turn).

## Grenades and mines

- Log after the first throw: "Throw aim ready" (not "velocity setter not found").
- Holstered and drawn, standing and moving, look away from where the body faces
  and throw a grenade: it lands on the crosshair ("Throw: projectile turned N
  degrees to the crosshair").
- Place mines holstered, right after drawing, and with the gun relaxed: each
  lands in front, toward the crosshair.
- Standing, look 45 and 120 degrees to each side and throw: the legs stay, the
  upper body twists; past the limit the legs follow, then turn in place after.
  Twist 0 in MCM: the whole body turns instead.
- Moving while throwing: you keep moving where you steer.
- Hold Melee, Alt-Tab out and back: the body is not stuck facing the camera.
