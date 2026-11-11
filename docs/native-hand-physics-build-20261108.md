# Native hand physics: build log (started 2026-11-08)

Branch `native-hand-physics`, from `hand-interaction` at `1d947f8b`. Analysis behind it:
`docs/hand-physics-lag-weaponstop-threading-20261108.md`. Collision history: `mods/cet/CyberpunkVRPort_HandCollision/COLLISION_NOTES.md`.

This file is read and updated throughout the build. Labels: **proved** (read from the DLL or source, by address or code),
**measured** (in game), **unverified** (built, never run in game).

## Where it stands (2026-11-09, last install: replacement on every grab, stage 1)

Installed and played with `xr_native_physics=1`, `xr_carry_lead=0.000`. Nothing committed.

| Topic | State |
|---|---|
| Gun and hand solve, world lists, `simulate` detour | Running in game. User: much smoother; hand pushing works |
| Crash on letting go of a two-body prop | Not seen since hiding held things from queries stopped. Mechanism not found |
| Held thing on the hand | Solved: a mesh copy on the player's carrier components `vrp_hold_left/right`, rigid on the hand slot. User: stays perfectly in the hand |
| Flips of the copy | Solved: game animation turned the slot up to 180 deg (measured); `SlotPass` holds it still |
| Collider on the hand | Solved: no weight limit, no one-frame prediction. User: fits perfectly |
| Against a wall | Hand stops with the collider (`ApplyHandStop`). User: behaves better |
| Walking drop | **Open, cause narrowed.** The prop instance is streamed out at a distance from its placement that goes with the mesh. See "The drop distance goes with the mesh" |
| Crash on closing the game | `exe+0x1C2F8F`, every exit, older than this work. Set aside by the user |
| Skippy: gun and hand jump at the body | 2026-11-11: something moving that follows the weapon stopped the gun 35 times in 2 min. `FollowsHeld` (`DebugView.cpp`) installed 16:57: did not help (never fired). 17:02: the 0.20 m sphere (query word2 `00400000`) seen at each Skippy draw is no obstacle, with a probe; DLL only. **Solved** (user, 17:14): the probe showed the sphere (r 0.10 m) centred on the gun's grip. Details in `COLLISION_NOTES.md` |
| GPU crash 2026-11-11 17:59 | **Open, cause not proved.** Device removed after 56 min (Dogtown, q302_reed "03_wait"); RED4ext: `gpuApiDX12Error.cpp:42` "Gpu Crash for unknown reasons". No nvlddmkm/TDR event in the System log, so likely a GPU fault, not a timeout. VRAM fine (9.3 of 15.3 GB). Last lines before it: a native recenter pair, then the two DLSS-G frame-gen features (`xr_framegen_backend=2`) rebuilt at the same size (so a camera flag changed), then `[submit-chain] chain stalled` (a symptom). The same rebuild ran ~12 times earlier that session without harm: a suspect only. Unrelated to the Skippy probe (CPU side). DRED was on but dumped nothing: the game's crash handler ended the process before our Present check saw the removal |

Ideas tried and rejected in this build: kinematic carry (not drawn); putting off the release of held actors (did not stop the crash); re-sending velocity through the game (did not stop it); taking held shapes out of contacts (did not stop the release, and the user requires held things to collide); wrist lead on the picture (no gain felt).

## Target

1. One predicted pose per frame, taken just before `PxScene::simulate`, used by physics, gun, IK and drawing.
2. Carried bodies driven kinematically from a collision-limited target; the picture retired. **Changed by testing:** driven by exact velocity, since kinematic bodies are not drawn; the picture came back as a same-frame correction.
3. Gun and hands solved by a 6-DOF proxy (rotation swept, rotation answer, real skin, crease handling).
4. Frame-locked native step; costly queries on workers publishing immutable snapshots; Lua events only.
5. Record-and-replay rig for offline regression tests.

Rules kept: no install without the game closed and a verified backup; no commit or push without the go-ahead; collision quality not traded for speed.

## Status

Written before the first test; "unverified" in this table is out of date for everything the summary above says has run.

| Part | Where | State |
|---|---|---|
| 6-DOF proxy solver | `include/Physics/ProxySolver.hpp` | Built, **22 tests pass** (boxes against the game's PhysX, triangles by the rig's own tests) |
| Offline rig and replay reader | `tests/physics` | Works. Recording from the game is unverified |
| Solver used by gun, hands and carried bodies | `DebugView.cpp` `ProxyMove`, `MoverWorld` | Built, unverified |
| World lists published under a lock | `DebugView.cpp` `worldLock` | Built, unverified |
| Gun and hands solved in the animation pass | `PhysicsRuntime.cpp` `AfterHandStop` | Built, unverified |
| `simulate` / `fetchResults` detours and `[physics]` probe | `PhysicsRuntime.cpp` | Built, unverified |
| Moving list at step start, static scan on a worker | `PhysicsRuntime.cpp`, `RuntimeCollect`, `RuntimeScan` | Built, unverified |
| Carry by exact velocity before the step | `PhysicsRuntime.cpp` `DriveOne` | Built. Kinematic was tried in game and dropped, see below |
| Lua routed to native, old path kept as fallback | `init.lua` `weaponStop`, `handWorldOne`, `carryDrive`, `carryRelease` | Built, syntax checked only |
| Master switch | overlay "Native hand and gun physics", `xr_native_physics` | Built |

### Second pass (2026-11-08)

| Part | State |
|---|---|
| Moving-actor snapshot for `VRPickCone` and `VRNearBody` (`DebugView.cpp` `CollectNearActors`, 10 m round the chest, every second step). No script-thread actor walk while the runtime runs; the old walk stays as the fallback | Built, unverified |
| Picture retired: outline half moved to `ThingHighlight.cpp`, `xr_carry_picture` switch and overlay box gone. **`HeldPicture.cpp` was restored later the same day**, see "Second game test" | Superseded |
| Replay with triangle meshes: the recorder writes meshes and hulls within 1.5 m of the gun as world triangles (at most 4000 a refresh), the rig tests boxes against them | Rig tested (22 checks pass); the recording itself is unverified |

The rig's triangle tests are its own (separating-axis overlap, 2 mm marched sweep), not PhysX's mesh code. Cooking was looked at and left: `PxCreateCooking`'s parameters differ from the public 3.4 layout (extra fields before the tolerances scale, read from the DLL) and it would need several more unproved slots, for a test tool only.

### Spikes: where each one stands

| Section | State |
|---|---|
| `gun before`, world lists | Off the script thread when the runtime runs |
| `world rays` | Only runs when the native hands are off |
| `aim cone` | Actor walk gone. The game mass lookups for the candidates are still on the script thread (they are game calls) |
| `taking` | The actor walk in `VRNearBody` is gone. Its rays and game calls remain |
| `pointing` | The cone part as above. The wall ray and `recOf` remain |
| `marks` | Unchanged. `VRThingOutline` still walks actors, because a kept actor pointer can be freed; `VRThingHighlight` is a render command on change only |

**It is not established that those 40-70 ms sections are the cause of the spikes.** They were single worst values per 10 s. A stall of the whole thread (Lua garbage collection, the OS) would land in whichever section was running. The `[pick]` line separates walking from mass lookups, and `[cost]` should be re-read after the first test.

### Not done, and why

- **`resolveRig` (body and NPC capsules) is still Lua.** The maths is simple, but its inputs are built each frame from bones and the NPC reads, and its output goes through the smoothing and hold logic in `send()`, written against shaking. Porting it needs those moved together and tested in a headset. It is a steady cost (part of HandCollision's 1.6-2.4 ms), not a spike source.
- **Fingers, throw tracking and the two-hand carry target maths are still Lua.** They read game bones or feed Lua-side state. Native carry already takes the target from Lua every frame.
- **Lua shoves of loose things are still Lua**, including their wake-and-settle bookkeeping.
- Lua calls the game on the main thread regardless, so "Lua events only" can only be approached.

**First installed 2026-11-08 21:31** with the game closed (DLL, `init.lua`, `weapon_custom_boxes.lua`, hashes compared). Backup of the build before this branch: `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-213138`. Restore by copying those three back, game closed.

### First test in game

Install only with the game closed and a verified backup. Then read `log\cyberpunkvrport.log`:

1. `[physics] on:` appears. Then one `[physics] simulate:` line per 10 s gives thread, calls, dt and the sample's age. **If it never appears, the game uses another entry point**: everything stays on the script's way, by design.
2. Rifle held parallel to a wall: swing by the wrist, slide sideways, walk alongside. The barrel should stop and the wrist stay put.
3. Pick up a prop, walk and swing. `[physics] carry ...: native` must appear. (The kinematic lines this step named are gone with kinematic carry.)
4. Throw it, and drop it. Then try a corpse (kind 12), the least certain case.
5. Compare with the switch off. Judge cost with the debug view off.

To record for the rig, create an empty `physics_record.on` beside the plugin DLL; `physics_replay.txt` is written for 60 s. Run it with `physics_test.exe "<game>\bin\x64" <file>`.

With the native path on, `VRGunPush` is no longer called, so its `[gunshape]` timing lines stop; `[physics]` replaces them.

## Research log

**PhysX3_x64.dll, `NpRigidDynamic` vtable at RVA `0x20B2E8`, proved by disassembly 2026-11-08:**

| Slot | RVA | Function | Evidence |
|---|---|---|---|
| 37 | `0x4DCC0` | getLinearVelocity(out) | reads three floats at +0x15C |
| 38 | `0x4BD90` | setLinearVelocity(this, const PxVec3&, bool autowake) | 3 args, third kept in `bpl`, zero-vector test, wake path |
| 39 | `0x4D800` | getAngularVelocity(out) | reads +0x168 |
| 40 | `0x4BE10` | setAngularVelocity(this, const PxVec3&, bool autowake) | same shape as 38 |
| 45 | `0x4F1F0` | setRigidBodyFlag(this, flag, bool) | error string "RigidBody::setRigidBodyFlag: kinematic bodies with CCD", `and al,5; cmp al,5` (kinematic 1, CCD 4) |
| 47 | `0x4DED0` | getRigidBodyFlags(out) | already in the notes |
| 54 | `0x4AD30` | setKinematicTarget(this, const PxTransform&) | begins with the buffered-flag test at +0x17C, reads q and p of arg 2 |
| 21 | `0x4A8E0` | setGlobalPose | error string |
| 0 | `0x4A800` | release | slot 1 returns the type name; detoured |
| 57 / 59 | `0x4BCA0` / `0x4BD60` | getLinearDamping / getAngularDamping | checked by address at run time |
| 60 | `0x4BE90` | setMaxAngularVelocity | already in `RaiseSpin` |

**PxShape vtable** (entry for `getFlags` at RVA `0x20EAB8`): 32 `setFlag(this, flag, bool)` `0x67110`, 34 `getFlags` `0x67200`, 21 `getSimulationFilterData`. **In the exe:** `0xC5A664` the player capsule's `preFilter`, `0x3DDE38` / `0x3DE0E8` the raycast hit converter.

**Scene vtable:** simulate 56 `0x53760`, advance 57 `0x537A0`, fetchCollision 60 `0x53900`, fetchResults 61 `0x53980`. `simulate` starts `48 83 EC 48 48 8D 05 ...`.

**Callers of `simulate` in the exe:** a byte scan for `call [reg+0x1C0]` gives 246 sites, because every vtable has that offset. Static search cannot name it; the detour logs thread and timing instead.

**PhysX window for off-thread reads (from the 3.4 API contract, not measured):** between `simulate` and `fetchResults`, scene writes from other threads are buffered and released objects stay allocated until `fetchResults`. So a worker that only reads actors in that window, and is joined at the start of `fetchResults`, cannot see an actor freed.

**OpenXR:** controller poses are already located at `handLocateTime`, a predicted display time (`OpenXRFrameLoop.cpp:1658`), so the hand target already contains prediction. No second prediction layer is needed for the arm.

**Job order:** physics flush is bucket stage 4, animation stage 8 (`cp2077-native-job-order.md`). Where `simulate` falls in that order is not documented.

**A latent bug in the sweep-normal search (found by the test rig, 2026-11-08):** `GunMove` finds the normal's offset by scanning from +16 for a unit vector. A hit can satisfy that at +32 (end of the position, start of the normal), as the first test sweep did: normal `(0.05, 0.05, -1.00)`, distance 0. The game's log has always said +40, so it was lucky. +40 is now tried first.

**Solver results against the real `PhysX3Common_x64.dll` (`tests/physics`, boxes only, 2026-11-08): 17 checks pass.** Includes: side-on wrist swing stops at the wall with the wrist unmoved; a thin sheet at 2000 deg/s is not passed; a post that the muzzle jumps in one frame stops the new solver and is jumped by the old behaviour; corners; grazing slides keep the 2 mm gap; about 0.02 ms a solve. Bugs it caught: (1) each step aimed at "all that is left", so the last step took back everything a contact had held; (2) past the turn limit a translate-only answer came out on the far side of a sheet; (3) a body arriving exactly touching never regained its gap. **Not covered:** triangle meshes (needs cooked meshes) and game poses.

## First game test (2026-11-08, build installed 21:31)

**User:** everything works and is much smoother. Carried things: only the collision is carried, the mesh stays behind and is back on the box after release.

**Measured, from the log of that session:** `simulate` runs once a frame, 450 calls per 10 s at 22 ms, so no substepping, on varying job threads. The hand sample is about 4 ms old at the step. The step's native work is about 1 ms; the worker 0.05-0.1 ms; `fetchResults` waited for it in a few of 450 steps, under 1 ms. No fallback line ever fired.

**Cause:** the game's position query followed the kinematic body (so the "does the game follow" check passed, wrongly), but the renderer does not take a kinematic body's moves. Inferred from the symptom plus the log; the render path was not traced. **Kinematic carry is dropped.** The body stays dynamic, and just before the step gets `(target - pos)/dt`, corrected for its damping (slots 57, 59, proved by address) and gravity, with the spin limit raised (slot 60). The `[physics]` line now says how far each step landed from its aim.

## Second game test and the same-frame mesh (2026-11-08)

**User:** the mesh draws, but is not glued to the hand; it trails a lot on a wrist turn. **Log:** the body landed a mean of 5.4-5.5 mm from its aim in two windows, 15-19 mm (most 101) in two others. That compares body with aim only.

**HIGGS, source read in part (`hand.cpp`, `physics.cpp`, `constraint.cpp`, `hooks.cpp`):** items keyframed from the hand, ragdolls and actors by a palm constraint, hand smoothing by `enableHandDelay`. Keyframing is not open here: this renderer ignores it.

**Built (not run):** `handFix` measured at the grip while still, then frozen (running, it took in a frame of each wrist turn; inferred from code). `HeldPicture.cpp` restored for kind 10, called from native carry. The mesh is drawn at the animated hand bone plus what the solver held back (`Correct`: world position and turn between the free target and the solved pose), which replaces the blocked blending and the reach ray. Where no mesh can be drawn on the hand, the hand goes to the body, capped by `K.HAND_TRAIL`.

**Open:** corpses and other kinds have no pose records. `VRNpcGrip` poses a living NPC through animation and does not apply to a ragdoll; writing limb bones before skinning is unresearched. Motion vectors of the picture remain unproved, and the DLSS ghosting in the old notes may return.

## Crash on letting go of a two-handed prop (2026-11-08 21:59:09)

Dump `%LOCALAPPDATA%\REDEngine\ReportQueue\Cyberpunk2077-20261108-215909-32596-39720`, read with a small script (no debugger installed). Access violation at `exe+0x3DE106`, `call [rax+0xA0]` (slot 20, getGlobalPose), in `0x3DE0E8` called from the hit converter `0x3DDE38`, for hits of a scene raycast (`[rax+0x2F0]`, slot 94), on a job thread.

**Measured:** the object's memory still holds proxy 103575, body 1 at +0x10/+0x14, the prop being held, but its first eight bytes are a heap address where the vtable belongs: consistent with a pool free-list link, so **the actor had been released** and a query still returned it. Who released it is not in the dump.

**Fixed, from reading the code:** (1) on a change of hand the frozen `handFix` of the old hand stayed in use, so the target jumped by the difference between the hands. Whether that broke the prop is inferred. (2) A body freed while held left a stale `c.actor`. A detour on `NpRigidDynamic` release (slot 0, RVA `0x4A800`) now drops it, and logs the call stack for a carried actor or one let go under 5 s ago: `[physics] the actor of ... was released`.

**Not explained:** why a released actor is still returned by a query. Also a separate crash report at 21:44:18 (`exe+0x1C2F8F`, reading +0x250 of a null global) in an earlier session, not looked into.

## Second crash, same prop (2026-11-08 22:08:16)

**The release watch named it:** `the actor of 78490/1 was released soon after (297 ms...)`, then `78490/0 ... WHILE HELD`, both from exe `7255a5 28d3fb 28c684 bb81a0 20bac52 4ba7b8 4ba5f3 2735e3`, on job threads. `0x72557C` is a smart-pointer reset calling the object's destructor; the rest has no strings, so what triggers it is **unknown**. Same prop as the first crash: a 10.13 kg, two-body `InstancedDestructibleNode` at (-1587, -2562, 83), proxy id differing by session.

Crash at rip 0, called from `exe+0xC5A678` (`call [rax+0xA8]` on a query's actor) under a PhysX character-controller sweep. Same pattern as the first: a query reaching a released actor, in the same second as the release.

**Containment, cause not explained:** an actor held or let go under 5 s ago is released at the start of the next `simulate` detour instead of when the game asks. Queries overlapping that moment are not excluded. If the game next crashes elsewhere on the same prop, this was not enough.

**A 3.7 kg box moved the arms:** my change had counted weight lag as "held back", so the hand went to the thing. Only what the world holds back counts now; weight stays in the mesh's position.

## Third crash, same prop (2026-11-08 22:15:11)

The put-off release ran and the game still crashed at rip 0 from `exe+0xC5A678` under the character-controller sweep, so **putting the release off did not help and was removed.** Correction: the pointer there is called at `+0xA8`, slot 21, which on a shape is `getSimulationFilterData`, so it is a released **shape**, not an actor.

Pattern, three sessions: the 10.13 kg two-body `InstancedDestructibleNode`: one body released 0.3-0.7 s after it was let go, the other while held. The 7-body prop, carried 10 s, was never released.

**Hypothesis, unproved:** native carry bypasses the game's command buffer, so the game may count the proxy as idle and tear it down. Lua now re-sends the body's current velocity through `VRBodySetVel` while native holds it. If it still crashes the hypothesis is wrong; compare with the master switch off on this prop.

**Box overshoot:** the weight limiter chased `(target-place)/dt` under an acceleration limit, with no braking, so it passed the target (read in code). It now limits speed to `sqrt(2 a d)` on top of the target's speed. `K.CARRY_EASY` raised from 3 to 6 kg: below that nothing is limited.

## Crash with the native carry off (2026-11-08 22:22 and 22:24)

**User:** crashed with the master switch on and with it off. **Log of the off run:** 0 `[physics] carry` lines, and a crash at rip 0 two seconds after taking the 10.13 kg prop. So **the native carry is not what starts this crash, and the velocity re-send did not stop it.** What both runs share is the script: grabbing, `bodyWake`, `VRBodySetQueryable`, the kinematic toggle.

Test in this build: bodies from 8 kg up (`K.QUERY_MOST`) are not hidden from queries. If the prop stops crashing, the query toggle is the cause. It has not been established whether the crash exists on the build before this branch; backup `pre-gun-turned-20261108-213138` can show that.

Other crash reports: 16 of the 17 dumps earlier on 2026-11-08 are at `exe+0x1C2F8F` (null global, +0x250), not this crash.

**Wrist turn:** the mesh took its turn from `g_VRFKSnapRot`, refreshed only in some passes. It now uses the turn the arm is solved with, through the hand frame measured at the grip. `[picture] ... let go` gives how far the two were apart. If that is near zero, the snapshot was not the cause.

## Test with heavy bodies not hidden from queries (2026-11-08 22:30)

**User:** no crash on the 10 kg prop; the box behaved better. One session only, so the query toggle as the cause is likely, not proved.

**Hands stopped shoving:** `GunCollectInner` returned at once when `gunMover.count==0`, a guard from before this branch. `[world]` showed 0 moving and 0 loose for the session except while a gun was out (`12/18/18`), and no `hand, loose` contact in the log. The guard is gone; the list is built whether or not a gun has been drawn.

## First run with the native path on after the fixes (2026-11-08 22:46)

**User:** walking pushes a held object out of the hand in the direction of walking, and it can fling out.

**Measured:** arm's turn against the skeleton snapshot 0.0 deg mean, 0.1 most, so **the snapshot was not the cause of the wrist lag**. Body against hand at submit 7-8 cm mean on 0.3-0.8 kg props. Landed off aim 0.7-41 mm mean by window.

**Cause, from reading the code, not measured:** `Publish` took the hand's speed between passes at least 4 ms apart. The player's place changes once a frame, so a frame's walk could be divided by a few ms and the body aimed that far ahead. Speed is now taken over at least 12 ms from a kept base pose.

The velocity re-send through the game is removed: it did not stop the crash, and it could have applied a stale velocity.

## Native run, 2026-11-08 22:54

**User:** the 10 kg prop no longer crashes with native on (query hiding off from 8 kg). Walking forward, a held thing eventually falls out; the hand-speed fix did not change that. The box still lags the wrist a bit. The crash on closing the game has always been there (`exe+0x1C2F8F`) and is not to be chased now.

**Falling out:** `let go: 0.0 m/s` lines are forced releases. The hand-lost check compared the hand with a predicted body. Native now counts only what the world holds it off by. Every forced release now says why: `the hand lost it` or `made to let go`. Whether this was the cause is to be read from those lines.

**Wrist:** with the mesh rigid on the bone, what remains is unproved: the solver turning it (`turn the solver held back` in `[picture]`), or the message at the submit reaching the screen a frame late. The overlay's lead (`xr_carry_lead`, 0 in the user's ini) now turns the mesh ahead by that long, native path only.

## Old-path leftovers dealt with (2026-11-08)

| Leftover | Now |
|---|---|
| `SetIsQueryable` on what is held | Off (`K.QUERY_MOST` 0). `exe+0xC5A664` is the capsule's `preFilter(this, data, shape, actor, flags)`, returning 0 or 2 (read): detoured, it ignores what is natively held and any freed shape or actor (vtable outside PhysX). With the native carry off, a held thing can stand in the player's way. **Rays now see what is held.** |
| Kinematic flag written directly every step | Only after the game has left it kinematic for 12 steps; logged. |
| Throw sent twice | Once, natively. The game's queue only if the native carry did not have it. |
| Gun's old path on a frame where native was not ready | Nothing done that frame. |
| Hands on the old rays while the world list is not ready | Kept (no other collision then); each change logged. |
| 200 ms without the script | 750 ms, logged. |
| Double shove | Not changed: `bodyPush` only adds what the body lacks, so it is not additive. |
| `resolveRig` / `send()` | Not ported. Measured instead: `script's stop on the arm` in `[physics] health`, with direction reversals (shake). |

`[physics] health` every 10 s; also `[HandColl] the hand lost it` / `made to let go`.

## Walking drop traced (2026-11-08 23:07)

The new reasons said `the hand lost it: native 0`, so native had already lost the body, and `[physics] the actor of ... was released WHILE HELD` came before it (65552/0, 73469/1, 73469/0). Landing error was large at the same time: mean 64 mm in a window, against 4 mm when free. `capsule ignored held` was counting, so queries were not the path.

**Inferred, not proved:** the body still had simulation contacts with the player's capsule, which shoved it and, on a destructible, may be what makes the game release it. While held its shapes now have `eSIMULATION_SHAPE` off (slot 32 `setFlag`, RVA `0x67110`, proved by its OR / NOT pattern and by address at run time), restored on release. A held thing no longer knocks other things over. Check: `released while held` and the landing error should fall.

The solver holds back 0.0 deg on the wrist (`turn the solver held back`), and the arm's turn equals the snapshot's, so the mesh is rigid on the bone. The remaining lag on fast moves has not been tested against the lead: `xr_carry_lead` was still 0.

## Contacts theory wrong (2026-11-08 23:15)

With every held shape out of the simulation, the game still released a held actor (77338/0) and it still dropped. **So contact with the player's capsule did not cause it.** Reverted: the user requires a held thing to collide with other things.

Two release paths, no strings on either: `7255a5 28d3fb 28c684 bb81a0 20bac52 4ba7b8 4ba5f3` and `cb3252 cb3115 cb30d4 27d3b2 72556a 7238cc 723650 2e1f15 2e1e89 4ba6bf 4ba522`, both on job threads, the second one in the walking drops (65552/0, 77338/0, 6-8 s after the grip).

**Next hypothesis, unproved:** the node's streaming. The exe has `m_streamingDistance` and the like for world nodes, and the drop comes after walking. `[physics] released while held:` now gives the seconds held and the distance from the grip. A repeatable distance would support it.

Also from that run: `turn the solver held back` reached 61-69 deg most, 5-8 deg mean on some carries, so the solver does turn things in contact. Landing error still up to 67 mm mean in a window.

## Walking drop measured (2026-11-08 23:25)

**User:** dropped 2 or 3 times while walking; never when standing still. Held objects collide again. A lead on the mesh did not feel better at any value tried, and the user thinks this needs a different approach.

**Measured at each release while held:** player 33.6 m from where it took the thing after 7.5 s; 40.8 m after 7.9 s; 19.6 m after 16.8 s; 41.3 m after 8.5 s. All four through the second release path (cb3252 ... 7233ec ... 4ba6bf). Never below 19 m in this run. **This supports unloading by distance from the prop's original place. It does not prove it**, and the distance is not constant, as streaming distances differ per node.

Any fix that keeps these world-instance props under their node inherits that: their lifetime belongs to the world sector, not to the hand.

## Hand carrier proof, built, not run (2026-11-08)

`vrp_hold_left` / `vrp_hold_right` already exist on the player template (`tools/ent/add_hold_components.py`), bound to `WeaponLeft` / `WeaponRight`, used by the reload for magazines. That tool's header records the same race measured on 2026-08-16: a thing placed by script reaches the screen a frame behind the drawn hand; a component on the player's template is carried in the hand's own pass. Runtime `AddComponent` does not stick, so use these.

On a grip of a kind 10 prop: `VRHeldMeshOnto` finds the world node inside the instance (by the class's vtable, offset logged), copies mesh and appearance to the carrier, Toggle on, local transform in the slot's frame for the first 0.35 s, then frozen. After 0.15 s the real mesh is drawn 1000 m below (`VRCarryHide`, through the pose records). On release all of it is undone. `VRCarrierDirty` tells the reload its record of the carrier is stale. Failures leave the picture as it was. Log: `[carrier]`.

Open: drawn in `RPl_Weapon`, as magazines are, so it is not depth-tested against the world in the usual way. One carrier per hand, so a two-handed hold rides the first hand.

## Hand carrier proof crashes on the grab (2026-11-08 23:39:33 and 23:42:24)

Installed 23:37. User: grabbing a loose object crashed the game, twice out of two tries. **So the proof has given no answer yet on whether the copy sticks.**

Both dumps (`Cyberpunk2077-20261108-233933-32856-41216`, `-234224-32252-33152`): access violation at `cyberpunkvrport.dll+0x16CC54`, on the script thread, which is `NodeOfGuarded` in `src/Natives/EngineObjects.cpp`, `cmp [rcx],rdx` at the field offset `r9=0x38`.

Measured from the dump, the instance of proxy 69840 (`0x2bc23168170`):
- `+0x38` is `0x42A655CF` in its low 32 bits, zero above: the float 83.17, the prop's height (the log has z = 83.17). The other crash read `0x42A6E62A`, the same kind of value. The scan took a position for a pointer.
- `+0x30`, `+0x48`, `+0x50` are float pairs (rejected by the `>>47` test), `+0x58` is `0x3F800000` (a float 1.0: passes every present test and would be read too).
- `+0x60` holds the world node, as the log says (`node at instance+0x60, mesh hash 50de2818a2a49a1a, appearance default`).

**Not explained:** the `.pdata` scope table for the function does cover `0x16CC54` (`[0x16cc54,0x16cc65)`, filter constant 1, target `0x16cc65`), so the inner `__try` was built correctly, and the fault still ended as "Unhandled exception". Inferred, not proved: another handler in the process acts on the first-chance fault. Also not explained: the native log has one successful scan of this proxy just before the crash, so the fatal scan was a later call; `carrierTick` keeps one state per body record, so a second body of the same prop would be a second call, but only `carry 69840/0` is logged. **Do not rely on `__try` as the only protection for a read of an unknown pointer in this process.**

The Lua `[carrier]` lines of that session are missing from the mod's log (nothing after 23:42:23 got there), so what the script did after the first scan is unknown. Everything after the scan (mesh swap, Toggle, slot transform, hiding the real mesh) has never run to a result.

**Fix, built and installed 2026-11-09 00:24 (DLL only, hash matched; backup `E:\cyberpunk_vr_portackups\pre-carrier-scan-fix-20261109-002439`), not yet run:** `NodeOf` never dereferences a candidate. It requires a real heap address (at least 4 GB, 8-aligned, under 2^47) and reads through `ReadProcessMemory`, which returns an error instead of raising a fault. Native `[carrier]` lines now number each scan and give the hand (to answer why the scan ran twice), and mark the mesh set, the reload and the hiding of the real mesh, because the Lua log loses its last lines in a crash.

## Hand carrier works; the collider trails (2026-11-09 00:24 build, played to 00:28)

User: picking up works, no crash; the visible object stays perfectly in the hand, also while moving; shape and colour right; reload after carrying fine. **New problem: the collider stays behind the hand and takes very long to catch up**, so it collides wrongly, and on letting go the object appears at the collider. This answers the carrier proof: **rigid on the hand slot removes the visual lag.** Not reported yet: pushing a held thing into a wall.

Measured: scan at `+0x60` on all 11 calls. `[picture]` body behind the hand: 17.48 kg one hand mean 0.036, most 0.382 m; 0.79 kg with two hands mean 0.108, most 0.399 m.

Cause for heavy things (from the code, the numbers computed, not measured per frame): `carryAccel` in `init.lua` limits the body's acceleration, and `DriveOne` applies it. 500 m/s2 up to 6 kg, then `500*(6/m)^5.4`: about 106 at 8 kg, 32 at 10 kg, **1.6 at 17.48 kg**, 0.08 at 30 kg (one hand). At 1.6 m/s2 a body needs over 2 s to reach walking speed. That limit was the weight feel while the body was what was drawn; with the copy rigid it only separates collider from picture.

Not explained by that: the 0.79 kg box has 500 m/s2. In that carry the carrier swapped hands repeatedly (calls 5 to 10, mesh reloaded each time). Corrected reading of the log: `hands` is built left first, so the copy jumped to the left hand whenever the left joined; the other swaps are real hand-to-hand transfers (`the other hand has it`). Second-hand grabs there were at 0.29 to 0.55 m from the palm, which suggests a hand reaching a collider that had been left behind (inferred). Whether "behind the hand" for that box is real lag is not established.

**Done, built and installed 2026-11-09 00:34 (DLL and `init.lua`, hashes matched; backup `E:\cyberpunk_vr_port\backups\pre-collider-follow-20261109-003436`), not yet run; `init.lua` not parsed by a Lua tool before install:** `DriveOne` has no limiter: the command is the hand's target each step, walls still stop it through `MoverSolve` (the old limiter was also unsafe without a biting limit: `targetVelocity + gap/dt` lands a step ahead). `carryAccel` now only limits the throw and the non-native path. The carrier stays on its hand while that hand holds the thing. New line at each release: `[physics] carry P/B let go: landed off its aim ...; the world held it back most ...; the driving hand changed N times`. **Heavy things now follow as easily as light ones while held: user's decision 2026-11-09.**

## Collider follows; a small lead and flips of the copy (2026-11-09, build of 00:34)

User: the collider stays much closer, not 100%, and sits a bit ahead in the direction of movement. All other checks fine (release position, collisions while held, wall, two hands, swings). New: the visible copy sometimes flips rotation briefly and returns.

Per-release lines from that session: landed off its aim mean 0.4 to 13.8 mm, most 137 mm, so the body gets where it is sent; the world held back up to 488 mm in contact.

**Lead, from the code, not measured per frame:** `DriveOne` still extrapolates the hand sample by `ahead` = one frame (`h.period`, about 22 ms, capped 50 ms) in position and turn. That was for a body that was itself the picture. It puts the collider about 44 mm ahead at 2 m/s, along the motion, as seen. If simulate and fetchResults both fall inside the frame whose hand is drawn, the prediction is one frame too many (inferred from the sample age at the step, mean 3.8 to 4.4 ms).

**Flip, cause not established.** After the first 0.35 s the carrier code writes nothing to the copy (`SetLocalTransform` only while `age < 0.35`), and the log shows no `off`/`on` in those carries. The reload's `holdPlace`/`holdShow` also write to these components, only from its own magazine steps. Leading suspect, inferred: the copy is bound to the `WeaponRight`/`WeaponLeft` slot bones, whose local transform comes from the game's animation unless a grip is being applied. VRIK places the wrist, not the slot, so a short game animation on the slot would turn the copy and not the hand. Nothing here pins the slot during a carry.

**Done, built and installed 2026-11-09 (DLL and `init.lua`, hashes matched; backup `E:\cyberpunk_vr_port\backups\pre-slot-hold-20261109-004836`), not yet run:** (1) `ahead` removed from `DriveOne`; the body goes to the hand as sampled. (2) `SlotPass` in `HeldPicture.cpp`, called in the pose pass in `AnimPose.cpp` after the finger layers: the first pass after the script's ask (`VRCarrierSlotHold`, every tick, ends by itself 250 ms after the last ask) remembers the slot's parent-local transform, later ones write it back. At the end of each hold: `[carrier] hand N: slot held F frames; the animation turned it more than 15 deg in T of them, most D deg, moved it most M mm`. That line tests the suspicion: if T is 0 while flips are still seen, the slot is not the cause.

## Lead and flips fixed; a held thing against a wall (2026-11-09, build of 00:48)

User: collider position fits perfectly, objects no longer flip. So removing `ahead` was right, and the frame-order inference with it. With the debug view on, a yellow capsule flips occasionally by itself with an empty hand.

**Flip cause proved by the new log line:** under a held thing the game's animation turned the slot bone by up to 179.9 deg, on 3 to 7% of frames in long carries (for example 162 of 3257 frames), and moved it at most 2 mm. Holding the slot still removed the flips.

**Wall, reported:** pushing a held thing into a wall leaves collider and copy out of line afterwards, and it does not come back. User's expected behaviour, adopted: **collider, copy and hand all stop at the wall; the hand does not go further.**

Why they part today: the copy is rigid on the hand, the hand follows the tracked controller, and the collider is stopped by `MoverSolve`. Lua's `H.dx` moves the hand only while `rec.pictured` is false, by at most `HAND_TRAIL` 0.25 m, and with no turn; while the world held the thing back up to 554 mm in this session.

Why it stays out of line afterwards: **not established.** Candidates: (a) `carryAlone` (two hands to one, `init.lua:3110`) rebuilds the grip from the body's current pose, so a body pushed off by a wall keeps that as its new grip, while the copy is only placed in its first 0.35 s; (b) solver state or the tether holding the body after contact. A one-hand wall push would rule out (a).

**Done, built and installed 2026-11-09 (DLL and `init.lua`, hashes matched; backup `E:\cyberpunk_vr_port\backups\pre-hand-stop-20261109-010703`), not yet run:** `AfterHandStop`, after `Publish` (the tracked hand still drives the target, no feedback), applies to the wrist the rigid move the solver gave the body at the last step, `wrist' = solved * free^-1 * wrist`, only with a copy on the hand (`VRCarryHands`), fresh within 100 ms, at most 0.6 m. The second hand of a two-hand carry gets the same. The script no longer moves that hand itself. `carryBase` sets `rec.carrierReplace`, and the copy is placed again for 0.35 s, delayed while a wall holds the thing off. Logs: the release line now gives `held off by more than 20 mm for N steps, M of them with no contact`; `M` high means candidate (b), the solver's own state. A hold-off of 1 s or more logs its length and depth when it ends. Letting go near a wall can snap the hand back to the controller at once; not eased.

**Noticed, not pursued:** one carry logged `landed off its aim ... most 1024000.0 mm`: exactly 1024 m in one step, the size of the origin grid seen earlier (origin -2048, -3072). A carried actor was moved by an origin shift between sending and reading. Worth testing against the walking drop, measured at 19 to 41 m walked. Unproved link. Also 2 carries ended by `the hand lost it` with `native 0`/`native 2`.

## Walking drop: four more (2026-11-09, build of 01:07)

| Proxy | Held | Player moved from the grip | Last sent to |
|---|---|---|---|
| 70039/0 | 7.8 s | 38.2 m | -1662.8 -2588.4 |
| 80482/0 | 3.5 s | 19.2 m | -1644.2 -2580.3 |
| 85439/0 | 3.7 s | 17.5 m | -1676.8 -2596.0 |
| 86615/1 | 9.5 s | 21.4 m | -1691.7 -2614.6 |

With the earlier 33.6, 40.8, 19.6, 41.3 m: **the distance from the grip is not constant (17.5 to 41.3 m), and time is not either (3.5 to 9.5 s).** Same release path in all eight: `cb3252 cb3115 cb30d4 27d3b2 72556a 7238cc 723650 2e1f15 2e1e89 4ba6bf 4ba522`, on job threads. The functions on it are short forwarders with no strings; virtual calls at `cb3252`, `27d3b2`, `4ba6bf`. Reading them statically did not name the owner. Each is followed by `the hand lost it: native 0`, so the drop is this release.

**World origin shift refuted for these:** every drop lies inside the same 1024 m cell as the origin (-2048, -3072), and no origin change is logged. The earlier 1024 m reading was a single carry and is unexplained, but it is not the cause of these drops.

**Distance from the grip is the wrong measure if this is streaming.** The dump of 2026-11-08 shows the instance holds a placement position at `+0x30` (floats -1636.55, -2580.53, 83.17 for the prop grabbed there). What streaming would use is the distance from the node's placement to the player, and the objects had been carried and dropped around before they were grabbed again, so the grip point says little. **Correction 2026-11-09: this node class has no `streamingDistance` field** (`world/InstancedDestructibleMeshNode.hpp`, `MeshNode.hpp`, `Node.hpp`); the earlier claim came from misreading a command's output. Streaming distance, if it is what decides, lies with the sector's data. Unproved until measured.

**How the guns and magazines avoid it (documented, `src/Natives/EntityAttach.cpp`):** they are entities, not world nodes. A held item's root component is a child of the player's slot component in the transform graph (`sub_14044764C`), so nothing places it and no sector owns it. Lobby items (`modules/items.lua`) are spawned entities attached the same way (`VRAttachToSlot`). Magazines are spawned entities (`reload.lua`) living only during the interaction. A loose world prop is a node instance owned by its sector, so that route does not apply to its body; the copy on the hand already uses it for the mesh, which is why the mesh is never the problem.

**Measurement built and installed 2026-11-09 01:17 (DLL only, hash matched; backup `E:\cyberpunk_vr_port\backups\pre-placement-log-20261109-011726`), not yet run:** `[physics] placement P/B taken|let go|RELEASED BY THE GAME: the player X m from where it was placed (...); the instance now says (...)`, for kind 10. The instance is re-read at each line, which also shows whether `+0x30` stays the original placement or follows the body (not established). Similar `X` over the `RELEASED` lines, and let-go lines below that, would support streaming; widely different `X` would count against it. If streaming is proved, the fix would be a physics body of our own during the carry, created from the prop's shapes, so nothing the game streams out can take it from the hand; on letting go, hand back to the real actor if it still exists.

## Placement measured; objects slow to appear (2026-11-09, build of 01:17)

**`+0x30` is the original placement:** it read the same at every grab, let-go and release of a carried prop, so it does not follow the body.

Player's distance from the original placement when the game released the held actor, on the `cb3252` path: **19.2, 42.3, 20.7, 37.1 m** (held 5.2, 9.4, 4.1, 7.3 s). Objects let go normally were at most 10.9 m from theirs. So: no single distance, but it is always well away from the placement, and with the grip-based figures of the earlier sessions (17.5, 19.2, 19.6, 21.4 and 33.6, 38.2, 40.8, 41.3 m) the values fall into **two bands, about 17 to 22 m and 33 to 42 m** (the earlier ones are from the grip and only approximate the placement). This supports a threshold per object or per sector, as streaming would have. It is not proof, and a band is 12 numbers. **User, 2026-11-09: a thing that drops falls and stays.** So the prop goes on existing after the game has released the actor that was held. A plain unload of the prop, which should make it disappear, does not fit. Streaming of the physics alone is still possible.

A different release also occurred: 94403/0 **0.5 s after the grip, 2.3 m from its placement**, on `7255a5 28d3fb 28c684 bb81a0 20bac52 4ba7b8 4ba5f3`, the path also seen shortly after normal let-gos. Not distance related; what it does is not established.

**Slow to appear, reported 2026-11-09 for grabbing and dropping. Not measured; from the code:**
- Grab: the real mesh is hidden on a fixed 0.15 s timer (`init.lua`, `age > 0.15`) and `Reload(component,false)` loads the copy asynchronously. A load over 0.15 s leaves the hand empty until it arrives. `Reload` already queues a job that runs after the load, which can report that it finished.
- Drop: while hidden the real mesh's records are moved 1000 m down (`kHideBelow`). Inferred: the renderer then treats it as far away, and on return its LODs and textures must stream in. Also unproved: an engine update may not come at once for a body at rest.

**What takes over after the release, not established.** After each of the five releases no `[physics] carry P/B: native` line follows for that proxy, although the script keeps calling `VRCarryHold` for up to `HAND_LOST_FOR` 0.6 s and that would make a new carry from any actor found under the same proxy and body. So within 0.6 s `FindDynamicActor` found none. Yet something falls: a new actor later, or under another id. To measure: after a release while held, keep looking for that proxy and body for a few seconds and log when and whether an actor comes back. If it comes back under the same id, the carry can continue on it, which is far smaller than a physics body of our own. If it does not, the body of our own stays the plan, and the handover on letting go needs to know what the game put in its place.

**Built and installed 2026-11-09 (DLL and `init.lua`, hashes matched; backup `E:\cyberpunk_vr_port\backups\pre-load-watch-20261109-013604`), not yet run:** the real mesh is hidden 0.05 s after the copy's load has finished (`VRCarrierReady`), 2 s at most; `kHideBelow` is 4 m instead of 1000. Logs: `[carrier] hand N: the copy's mesh was in after X ms`; `[carrier] the real mesh is out of sight after X s (copy loaded|time limit)`; `[physics] after the release of P/B: an actor is back under that id after X ms, at the same|a new address, at (...)`, or `no actor under that id in 5000 ms`, looked for every 100 ms before a step.

## The drop distance goes with the mesh (2026-11-09, load and watch build)

User: objects now appear at once, on grabbing and on dropping (hiding after the load, `kHideBelow` 4 m). The copy's load logged 0 ms for all 9; the real mesh was hidden 0.05 to 0.09 s after the grab. A pizza box dropped at the same distance on both tries.

Player's distance from the prop's original placement at the game's release, `cb3252` path:

| Mesh hash | Proxy | Placement | Distance |
|---|---|---|---|
| `cdd32fc02eb4763f` | 73096 | -1668.6 -2592.4 | 21.8 m |
| `cdd32fc02eb4763f` | 75960 | -1668.6 -2592.4 | 23.0 m |
| `22345537ded596bb` | 65552 | -1677.0 -2598.8 | 33.7 m |
| `22345537ded596bb` | 72284 | -1668.0 -2592.4 | 33.0 m |
| `22345537ded596bb` | 81845 | -1668.0 -2592.4 | 32.2 m |

**The same mesh drops at the same distance from its placement, within about 1.5 m, also from a different placement (65552); different meshes differ.** The answer to "is the distance always the same" is: per kind of object, yes. A threshold from the mesh or its node, as streaming has, is now well supported. Still inferred; the number has not been found in the data.

**The instance is gone at the drop, and comes back as a new one:**
- No actor under the released id in 5 s, all five times.
- The same placement was taken again under a new proxy id: -1668.6 -2592.4 as 73096 then 75960, -1668.0 -2592.4 as 72284 then 81845. So after a drop the prop was loaded anew at its placement.
- At the release of 75960 the memory at instance+0x30 already read (-5.5 10.6 -0.0): it had been reused.

So: walking away from a prop's placement streams its instance out, and the actor with it. What the user sees fall and stay is not explained: not established whether it is still a physics object.

**This changes the plan.** A physics body of our own keeps the hand's collider, but the real prop is gone by the time of letting go, so the handover to the real actor cannot work past the threshold. Continuing on a replacement under the same id is ruled out. Something of the player's has to become the object. Options: (a) at the game's release, or at the grip, replace the prop by a spawned entity with that mesh and a dynamic body (lobby items are such entities, but authored ones; whether one generic entity can take any mesh's collision is unknown); (b) keep the instance alive past its streaming, by research on what decides it (risky: sector lifetime).

## Research: a spawned entity that becomes the carried thing (2026-11-09, no code changed)

**What exists (read from the repo):**
- `src/Natives/SpawnSystem.cpp`, `Game.VRSpawnTemplate`: the port spawns entities itself. `modules/spawner.lua`: nothing of it goes into a save, and nothing of it streams out with distance. That is the property wanted.
- Lobby items: an entity with an `entPhysicalMeshComponent`, Dynamic, whose body comes from the mesh's own `meshMeshParamPhysics.physicsData` (a `physicsSystemResource`: bodies, colliders, mass; `tools/vr_lobby_assets/make_lobby_scene.py`). `c:CreatePhysicalBodyInterface(0)` gives the body to the script, and they are carried and thrown already.
- `entPhysicalMeshComponent` has `simulationType`, `useResourceSimulationType`, `filterData`: Dynamic need not come from the mesh.
- `entColliderComponent` holds a list of `physicsICollider` (box, capsule, sphere, convex, mesh) with `massOverride`, independent of any mesh. The port's templates already use box colliders.
- The carry's solver works from `ActorParts`, a box per shape of the real actor, so box sizes per shape are known at the grip.

**Two questions decide the route, both open:**
1. Do the loose props' meshes carry physics the component can use? A kind 10 prop is a `worldInstancedDestructibleMeshNode`; its mesh may hold `meshMeshParamPhysics`, `meshMeshParamCompiledPhysics` (a deferred collection, unknown to the component), or none. **Could not be read here:** the bundled `tools/wolvenkit-9.1.0/WolvenKit.CLI.exe` needs .NET 10, which is not installed. Installing it is the user's decision.
2. Does the body follow a mesh changed after the entity exists? `VRComponentChangeResource` reloads the look; nothing shows it rebuilds physics. The body may be made at attach only.

**The routes:**
- **A. One generic template with `entPhysicalMeshComponent`:** exact collision, if question 1 and 2 both come out well.
- **B. A generic template with `entColliderComponent` (one dynamic box or a few) and a mesh component set to the prop's mesh:** does not depend on question 1. Box sizes would have to be changed at runtime, by writing the colliders before attach or by the PhysX shape after it. Coarser collision after letting go.

**Timing, the same for both:** a spawn takes some frames. Spawning when the game has released the actor leaves the hand with only the copy for that time. The copy needs no body, so it would not be seen, but a held thing would not collide meanwhile. Then the carry moves to the new body by its own `VRCarryHold` path.

**Question 1 answered 2026-11-09** (.NET 10 runtime installed by winget with the user's go-ahead; the four meshes of a session extracted by hash and serialized): **all four carry `meshMeshParamPhysics`, one body, `Dynamic`**, with mass and a real collider: `cardboard_box_open_c` box, 0.985 kg; `soda_can_c_trash` capsule, 0.349 kg; `trash_packaging_a` box, 0.766 kg; `trash_packaging_e` convex with a compiled buffer, 3.008 kg (the `22345537...` mesh that dropped at 32 to 34 m). The same structure as the lobby's items. `cdd32fc0...`, which dropped at 22 to 23 m, is `cardboard_box_open_c`. **Route A chosen by the user 2026-11-09, the way the port does it.**

**Proof for question 2, built and installed 2026-11-09 (DLL and `init.lua`, hashes matched; backup `E:\cyberpunk_vr_port\backups\pre-spawn-proof-20261109-015349`), not yet run:** `K.proofTick` in `init.lua`, on the first three grabs of a session: spawns `vrport\lobby\vr_lobby_item_2.ent` (a 0.085 m half-size cube) 1.2 m ahead and 1.3 m up, copies the held mesh onto its `item` component (`VRComponentCopyMesh`), logs the body's dimensions and position as spawned, 1.5 s after the mesh change, and after the component is toggled off and on with the body asked for again, then deletes it. All `[carryproof]`. If the dimensions stay the cube's, the body does not follow the mesh: the mesh must be set before the entity attaches.

## Spawn proof result (2026-11-09, build of 01:53)

Three runs, no crash. A lobby item spawned in the open world at once (under 1 s), `entPhysicalMeshComponent`, its body dynamic: it fell and came to rest (z 83.08 to 83.06). **The body does not follow a mesh changed after the entity exists:** `dims 0.172 0.172 0.172` (the cube) as spawned, 1.5 s after `VRComponentCopyMesh`, and after the component off and on with the body asked for again. So the mesh must be in the entity before it attaches. User: objects fine after dropping them and waiting. The walking drop remained, as expected: this build only measured (65766/0 at 34.0 m, `trash_packaging_e` again; 65820/4 at 35.5 m).

**The port's own way is one entity template per item** (`item_entities` in `make_lobby_scene.py`: the ball's `.ent` with the mesh path set, in the archive). Applied here: a template per prop mesh, named by the mesh's hash, so the held mesh's hash gives the template's path with no lookup table. Tooling exists (WolvenKit CLI works now).

**Size:** 9854 listed `.mesh` lines under `base\environment\decoration` (garbage 147, containers 596, food 263, furniture 855, electronics 842). Only those with a Dynamic `meshMeshParamPhysics` body need one. **Counted 2026-11-09, all serialized:** 5264 carry the parameter, 5227 with a body were read; **1537 have exactly one Dynamic body, 1769 any Dynamic body.** Bodies: Dynamic 10967, Static 3521, Kinematic 761. Shapes: box 12853, mesh 2692, convex 2061, capsule 942, sphere 46. All four meshes of the earlier session are single Dynamic bodies. A raw search of the file for the text `Dynamic` is useless here (128 hits, missing 3 of those 4): the value is not stored as a name when it is the default. Props outside `decoration` are not covered by this list.

**Open design points:**
- When to swap: at the game's release (the hand keeps only the copy for the frames the spawn takes, so no collision in that time), or at the grip (every grab spawns).
- The real prop reloads at its placement later (seen as new proxy ids), so a carried replacement left elsewhere is a second one of it. Whether that matters to the user is theirs to say.
- What is seen falling at the moment of the drop today is still unexplained.
- A prop whose mesh has no template keeps today's behaviour.

The proof code (`K.proofTick`, `VRComponentCopyMesh`) goes out with the real build.

**User's decision 2026-11-09: spawn the replacement on every grab,** so that collision is never missing at the moment it is needed. Design, not built:
- At the grip the real body is carried as today while the replacement spawns (measured under 1 s). When it is in, the carry changes body inside one step, so collision never lapses.
- While the replacement is carried, the real body must take no part: out of simulation or queries, hidden as today. How to do that without the game noticing is open.
- Letting go with the real instance still alive (short carries, inside the streaming distance): the real one takes over at the replacement's pose and velocity, and the replacement is deleted. Today's behaviour, no duplicate.
- Letting go with the real one gone: the replacement stays. When the player comes back within that prop's measured distance of its placement, the real one reloads, and the replacement is deleted (user was told: objects do not stay moved for good; carried back in the hand, both are visible until let go).
- A mesh with no template keeps today's behaviour.

## Replacement on every grab, stage 1 (built and installed 2026-11-09, not yet run)

Backup `E:\cyberpunk_vr_port\backups\pre-carry-swap-20261109-021810` (DLL, `init.lua`; `vrport_carry.archive` and `carry_templates.lua` are new). Templates only for the four meshes known dynamic: `cardboard_box_open_c`, `soda_can_c_trash`, `trash_packaging_a`, `trash_packaging_e`. Any other prop logs `no template for mesh` and behaves as before.

**Correction:** the tsv of serialized meshes has file names only (`convert serialize` flattens), so the earlier hash check against it found nothing. Joined with the archive listing, all 7 older meshes tried are under `decoration`. For the full set: 37 of 5264 were lost to shared names at serialization, and names shared by two meshes are left out.

Pieces:
- `tools/carry_templates/make_carry_templates.py <tsv> <archive listing> (all | hash...)`: the basketball's entity from the installed archive with the mesh set; `vrport_carry.archive` beside `cyberpunkvrport.archive` (the mount takes every archive in that folder); `carry_templates.lua`, the hashes that may be spawned.
- Script (`K.swap*`, `[swap]`): at the carrier's `on`, spawn `vrport\carry\<hash>.ent` 25 m below the prop; when in, hide it by dissolve (`VRComponentDissolve`, mode 2), wait 0.5 s, `VRCarrySwap`. The real body is carried until then. After the swap `rec.body` is the replacement's and `BODIES` has its key, so a second hand or a ray finds this carry.
- Native (`DriveOne`, `c.swapTo`): in one step, real body moved 20 m down and made kinematic, replacement put at the target. The carry keeps the real ids (script keys, picture). `realActors[]` is watched in `ActorRelease`, so a real body streamed out while held only logs `[swap] ... the game released the real body`.
- Let go (`LetGoNow`): real alive: it is made dynamic at the replacement's pose with the throw, replacement put away and deleted by the script. Real gone: the replacement stays, shown; `K.swapFrame` deletes it when the player is within the release distance minus 2 m of the placement.

Unproved until run: real body kinematic and 20 m down without the game objecting; dissolve on a physical mesh; setGlobalPose on a body the game owns; the two-hand path on a replacement; throw through the swap. `K.proofTick` and `VRComponentCopyMesh` are removed.

## Stage 1 played (2026-11-09, build of 02:18)

User: **no transition seen, and objects no longer drop out of the hands.** Log: entity in after 0.06 s every time; the real body released by the game at 19.1, 34.6, 34.5 m with `the carry goes on`, carried on to 82 m. Hand-back on short carries worked. No crash. New problems:

**1. Letting go far from the spawn point: it falls through the floor. Near it: normal.** In the log, every far let-go is `the replacement stays`, every near one `the real one takes over`. So the replacement does not collide with the floor. **Cause, strongly supported, not run:** the template has `filterDataSource = Collider`, so the body takes the filter of the mesh's own collider, and those differ per mesh: read from the serialized files, `cardboard_box_open_c` World Dynamic; `trash_packaging_a` **World Static**; `soda_can_c_trash` and `trash_packaging_e` **none** (the two that fell through in this session's far let-gos are of those). The lobby ball's collider has World Dynamic, which is why the proof cube landed. A world prop gets its filter from its node, which is why the real ones collide. In-hand collision goes through `MoverSolve`, not this filter, so the carry did not show it.

**Done 2026-11-09 (templates):** `filterDataSource = Parent` (the same value as Component) and the component's `filterData` is the ball's (`World Dynamic`, query 0/143769604, simulation 376836/5070). Not yet run.

**2. Two visible meshes at times, one with a collider and one without.** Most likely the replacement is showing as well as the copy: the dissolve reported `hidden true`, but a dissolve set right as the entity appears may not hold. Not established; when it happens was not reported.

**Done 2026-11-09:** the template has `visualScale` 0.001 and the dissolve is gone. When a replacement stays, `visualScale` is set to 1 and the mesh is reloaded through `VRComponentChangeAppearance`, logged as `[swap] shown again: true|false`. Whether the new size is then drawn is unproved. The settle before the swap is 0.1 s (it was the time for the fade).

## Filter and hidden scale played (2026-11-09, build of 02:28)

User: no mesh duplicate seen this time. Dropped far away, **the mesh collides with the floor briefly and then falls through.** So the filter helped (it now touches the floor) and something later ends the collision. Open; not yet looked into. Committed at this state on the user's request.

## Proposed next approach (2026-11-08)

A held thing becomes the player's: a plain `entMeshComponent` copy of the prop's mesh on the player, bound to the hand slot, drawn by the engine in the same pass as the hand, and the world instance hidden. The project already draws props this way (reload magazines, `init.lua` of the Reload mod). Physics stays a separate body for collision. Unknowns: reading the mesh path and appearance from a `worldInstancedDestructibleMeshNode`, whether a component added at run time renders in time, hiding one instance, and what replaces the body when the game unloads it.

## Decisions

- **The carry target extrapolates exactly one frame from the last animation sample.** Whichever order animation and `simulate` run in, the step's result is seen with the next animation's hand, so this needs no knowledge of the order.
- **Moving actors are gathered at the start of the `simulate` detour, not on the worker.** Poses are freshest there, and reads of moving actors during a step can tear. Only the static scan runs on the worker, in the `simulate` to `fetchResults` window.
- **Past 1.2 rad off the tracked turn, a contact holds the turn** rather than answering by translation.
- **Every native path falls back by itself** (slot proof fails, `simulate` never fires, game does not follow a kinematic body) and has one master switch, `xr_native_physics`.

## Not established

- Which thread calls `simulate`, whether the game substeps, and whether it uses `simulate` at all rather than `collide`/`advance`.
- Whether the game picks up a kinematic body's moves and draws them. The code detects a mismatch and falls back.
- Whether a ragdoll limb accepts being kinematic and whether the joints drag the rest as hoped.
- Whether `handRot` and the bone are the same frame. Measured at runtime and cancelled, so a difference is handled, but the two-handed rifle's second hand follows position only, so a rotation answer may separate it from the gun.
- The accuracy gained. Repeat the `[trace]` measurement from the analysis.

## Installs

- 2026-11-08 21:41: carry by exact velocity. Replaced files backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-214117` (that is the kinematic build). The build before all of this is in `pre-gun-turned-20261108-213138`.
- 2026-11-08: same-frame mesh for props, frozen hand frame. Replaced files (the velocity build) backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-215419`.
- 2026-11-08: hand-change fix and release watch. Replaced files (the same-frame mesh build) backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-220348`.
- 2026-11-08: deferred release of held actors, weight no longer pulls the hand. Replaced files backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-221213`.
- 2026-11-08: braking limiter, game velocity re-sent while held, put-off release removed. Replaced files backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-221906`.
- 2026-11-08: mesh turn from the arm's wrist, heavy bodies not hidden from queries. Replaced files backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-222837`.
- 2026-11-08: moving-object list built without a gun. Replaced files backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-223853`. The test before this ran with xr_native_physics off (no [physics] carry lines).
- 2026-11-08: hand speed over at least 12 ms, game velocity re-send removed. Replaced files backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-225002`.
- 2026-11-08: leftovers dealt with, capsule filter, health logging, walking drop check, mesh lead. Replaced files backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-230421`.
- 2026-11-08: held bodies out of contacts, xr_carry_lead set to 0.020 for a test (was 0). Replaced files backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-231120`; the ini in `vrport.ini.before-lead`.
- 2026-11-08: contact removal reverted, release distance logged. Replaced files backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-231907`.

## Reading the logs for this work

- **Check first that the native path ran:** `[physics] carry` lines, or `hands 0 calls` in `[world]`. Two whole test sessions were read as native when `xr_native_physics` was 0: an overlay checkbox unticked for a comparison stays off, saved in `config\vrport.ini`.
- `[physics] health` every 10 s; `[physics] the actor of ... was released`; `[physics] released while held:`; `[HandColl] the hand lost it` with `native 0` meaning the native carry no longer had the body; `[HandColl] made to let go`.
- `[picture] ... let go`: body behind the hand, arm's turn against the snapshot, turn the solver held back.
- Crash dumps: `%LOCALAPPDATA%\REDEngine\ReportQueue\<name>\Cyberpunk2077.dmp`. No debugger is installed; a small Python reader of the minidump streams gave address, registers, stack words, and memory at a pointer.
- 2026-11-08: hand carrier proof. Replaced files, reload.lua included, backed up in `E:\cyberpunk_vr_port\backups\pre-gun-turned-20261108-233718`.
