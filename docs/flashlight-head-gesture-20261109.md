# Simple Flashlight on the head, with an ear gesture (2026-11-09)

Status: built and installed 2026-11-09, **unverified in game**. Not committed.

## What exists

- `src/Hooks/XInput.cpp` + `EarTriggerGesture` in `include/Hooks/ScannerGesture.hpp`: support hand in the scanner's ear
  zone + its trigger counts up shared slot [175] (174 until the merge of upstream 0.2.3, which took 174 for `kReloadGlowOff`) (`kFlashlightToggleCount`). Switch: `xr_flashlight_gesture` in `vrport.ini` (default 1).
- `mods/cet-standalone/CyberpunkVRPort_Flashlight`: replaces the flashlight mod's `logic.updatePosition` at run time with
  the headset pose (`VRViewWorldPos/Rot`), and calls its `logic.toggle` when the slot changes.

## Non-obvious facts (read from the files)

- **Two Lua states.** The port's mods run in the plugin's own Lua host (`red4ext/plugins/cyberpunkvrport/lua`). The
  flashlight mod runs in the real CET (`cyber_engine_tweaks.log`: "Mod flashlight loaded!"). `GetMod` does not cross, so
  this mod has to be installed in CET's `mods` folder, which is why it is not under `mods/cet/`. Copied there by hand; no deploy script handles it.
- The flashlight mod's original pose takes position from the FPP camera and yaw from the player body, so it does not follow the head.
- Its license forbids reusing its code or forking without permission, and it is Vortex-managed: do not edit or copy its files.

## Behaviour chosen with the user

Toggle, not hold. The squeeze must start with the hand already armed at the ear. While it is owned, LT does not reach
the game. Off while the scanner is confirmed open (LT = zoom modifier), in vehicles (LT = brake), in menus and the lobby.

The mod's own controller toggle (hold D-pad left = the left trackpad held) switched the light on by accident; turned off
2026-11-11 at the user's request: `"controllerMode":false` in the installed `mods\flashlight\config.json` (its
"Controller Support" switch). The ear gesture calls `logic.toggle` directly and does not depend on it.

## Known limits

- The LT mask at the ear applies even if the flashlight mod is absent; only the toggle does nothing then.
- Possible one-frame lag of the light on fast head turns (teleport from CET `onUpdate`): not measured.
- The lighter (Smoking) reads the raw trigger; a flick inside the ear zone would also toggle the light.
