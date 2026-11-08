# CyberpunkVRPort_HandCollision

**Read `COLLISION_NOTES.md` in this folder before you change, test or measure anything about how the hands or the gun
meet the world.** It records what was measured in game, which bugs were found and how, which ideas were tried and
rejected (with the reason), the PhysX slots and offsets already proved, and the workflows that work. Do not re-test or
re-try what it already settles; if you have a new reason to, say so first.

When you learn something new here -- a measurement, a failed attempt, a proved offset, a user decision -- add it to
`COLLISION_NOTES.md` in the same change, in the section it belongs to.

Quick facts that bite:

- `init.lua`: `local K` and most locals live inside `local HC = (function() ... return {...} end)()`. The
  `registerForEvent('onUpdate', ...)` code after it cannot see them: use `HC.K.*` or export through HC's return table.
  A runtime error in onUpdate stops all hand collision every frame.
- The native side is in `src/Natives/DebugView.cpp` (movers, near list, moving obstacles, probes) and is registered in
  `src/Natives/Registration.cpp`.
- `weapon_shapes.lua`, `weapon_boxes.lua`, `weapon_attachments.lua`, `capsules.lua` are generated: regenerate, do not edit.
  `weapon_custom_boxes.lua` is written from Blender by `tools/weapon_blend/read_weapon_blend.py`: edit the `.blend`.
- Weapon frame: origin at the grip, +Y along the barrel, +Z up, metres. Lua model space to world:
  `qrot(-ii, -ij, -ik, ir, v) + b`; world to model: `qrot(ii, ij, ik, ir, v)` (with `bx..ir` from `worldFrame()`).
- Never judge performance with the debug view on (it costs 1.3-3.7 ms a frame by itself).
- The user's rules: no code change without approval; install only with the game closed and a verified backup; no commit
  or push without the go-ahead; collision quality must not be traded for speed.
