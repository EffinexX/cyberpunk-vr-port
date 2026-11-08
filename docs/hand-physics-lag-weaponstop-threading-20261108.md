# Carried-object lag, weaponStop from the side, and moving physics off Lua (2026-11-08)

Analysis only: no code, setting or installed file was changed. It answers this feedback:

> "You're replacing street body mesh with picture, and physics lags by tens of millimeters during walking or arm movements,
> its bad. weaponStop doesn't work from the side. overall physics should be divided in threads, not sitting only in lua
> gamethread it will give a lot of hitches, spikes and etc"

Reading of the first sentence, which is an assumption: "street body" means a loose prop picked up in the world. Its mesh is
redrawn on the hand by `src/Natives/HeldPicture.cpp`, while its PhysX body follows behind.

Evidence labels: **measured** = from the logs of 2026-11-05 to 11-08 on the reference PC (70k HandCollision lines, 45 real fps);
**read** = in this source or the game's DLL; **inferred** = reasoned from those, not yet proved in game.

## Verdict

| Claim | Verdict | Short version |
|---|---|---|
| The mesh is a picture and the physics lags | **Correct** | Median residual 25 mm over 30 traced swings. The usual delay is one game frame (22 ms). The picture hides it for one prop type only. |
| weaponStop fails from the side | **Partly correct** | Side stops do happen, but the gun's *turning* is never swept and the answer can only move the wrist. A side-on barrel is the worst case for both. |
| Physics should be split into threads | **Right direction, wrong diagnosis** | PhysX and the sweeps are already native. Lua's real costs are marshalling, the frame of delay, and one-off spikes. Moving the solve to the right engine stages fixes the lag; threads alone would not. |

**Biggest single lever:** today the chain is *hand sampled, Lua runs, command queued, physics steps next tick*. Solving the gun and hands
inside the animation hook, and the carried body just before `PxScene::simulate`, removes that frame and takes per-frame physics out of Lua together.

## 1. Why carried things lag

### The current chain (read)

1. `VRIK_ApplyHandStop` publishes the raw hand target during animation, stage 8 of the bucket order in `cp2077-native-job-order.md`.
2. Lua `onUpdate` runs later as a main-thread task (`LuaVM_Hooks.cpp:446`).
3. `carryDrive` (`init.lua:3143`) calls `VRBodySetVel`, which goes through script `SetLinearVelocity`. That only queues into the proxy's command buffer (`sub_140613320`).
4. The buffer is flushed at the next tick's physics stage (stage 4), and the step follows.
5. So a hand sample from tick N moves the body in tick N+1. The body is then compared against the hand of N+1.

### Measured

| Quantity | Value |
|---|---|
| Interval between carry updates | 22 ms median, 27 ms p95 |
| Best-fitting delay, target to body, 30 swings | 1 frame in 16, 0 in 8, 2-3 in 2, 5-8 in 4 (likely blocked or heavy) |
| Median leftover error after that best-fit shift | 25 mm |
| Velocity actually read / velocity told (median) | 0.87 |
| Raw target-to-body error, 0-0.5 m/s | mean 37 mm, p90 81 mm |
| Raw error, 1-1.5 m/s | mean 110 mm, p90 296 mm |
| Raw error, 4-4.5 m/s | mean 168 mm, p90 232 mm |

The trace only starts on fast swings (over 2 m/s), and its target already contains the lead, so the raw errors overstate ordinary carrying
and include blocked or heavy moments. One frame at walking pace is the simple check: 1.5 m/s x 22 ms = 33 mm, which matches "tens of millimetres".

### Why the existing note undersold this

`hand-interaction-20261105.md` says direct PhysX "would save one step at best, so it was not built". At 45 real fps one step **is** the complaint.

### What else adds error (read)

- `K.CARRY_FOLLOW = 0.85`: only 85 % of the position error is corrected per step.
- Heavy props are slowed on purpose: `carryAccel` falls with `(3/m)^5.4`, about 0.75 m/s^2 at 10 kg. This is a design choice, but it will read as lag.
- Lead is a straight-line guess (`K.CARRY_LEAD`, plus a step of walking), so it is wrong exactly when a hand accelerates or reverses.
- Hand orientation comes from `bone()`, the previous solved pose (`handReal`, `init.lua:2714`), while position comes from raw tracking.

### Where the picture does not help (measured and read)

- It only works for `worldInstancedDestructibleMeshNodeInstance` (kind 10). The log has "picture stays on its body" for kind 1 (7 times), kind 12 corpses (7) and kind 10 (2). Those show the raw lag.
- It blends back to the lagging body whenever the carry is `blocked` (106 "ran into something" lines).
- Collisions, shoves and contacts all happen at the lagging body, not at what the player sees.
- Motion vectors and ray tracing of the picture are still marked unproven in `HeldPicture.cpp`; the DLSS ghosting in the notes is open.

### Fix, in order

1. **Probe first (small).** Detour PhysX `simulate` (slot 56, RVA `0x53760`, re-confirmed from its error string this session). Log the owned scene only: thread, elapsed time, calls per frame, and timing against stage 4, the pose hook and Lua. Nothing yet says whether the game substeps or which thread calls it.
2. **Native carry drive, run just before `simulate`.** Body and target in C++, reading the freshest entity pose and hand data. Velocity is written straight to the actor, skipping the command buffer. `setLinearVelocity` and `setAngularVelocity` are **not yet proved** in this DLL: prove their slots by address, as `GunProveShape` does. Writing there is safe because the scene is between `fetchResults` and `simulate`.
3. **Deadbeat gain.** `v = (target at end of step - pos) / actual elapsed`, using that step's real elapsed time and gain 1.0 for light things. Keep the mass limit as the weight feel.
4. **Predict from the runtime, not by straight-line lead.** Ask OpenXR for the controller pose at the step's end time.
5. **Experiment, only if 2-4 are not enough:** carried light props become kinematic with `setKinematicTarget` (slot 54, known). That means no tracking error, but walls must then be handled by the same swept mover as the gun, and it turns dynamic again on contact and release. Lost contact response is the risk, so this is not the first choice.

**Target, estimated not promised:** under 10 mm in steady walking and about 15-25 mm in sharp arm reversals. The picture stays as the last-millimetre polish and as the only fix for rendering delay.

## 2. Why weaponStop fails from the side

### What the logs say

Sampled once a second, in body axes, non-zero pushes were: forward 930 (mean 26.4 cm), up 753 (8.9 cm), **sideways 323 (8.9 cm, up to 45 cm)**.
So sideways stops do occur. The defect is specific situations, not a missing direction. These samples cannot show the misses themselves.

### Causes, most likely first

| # | Cause | Where | Status |
|---|---|---|---|
| 1 | **Rotation is never swept.** `GunMove` applies this frame's `turn` before moving, then sweeps translation only. The code's own comment calls what the turn adds "a small thing". Side-on, it is the main motion: 200 deg/s x 22 ms x 0.5 m is about 3.8 cm at the muzzle per frame, with no wrist movement for a sweep to see. | `DebugView.cpp:806`, `:782` | Read; effect inferred |
| 2 | **The answer can only translate the wrist.** "The hand has only been moved, never turned." A barrel swung into a wall needs its swing stopped. Instead the arm slides sideways and the controller's rotation carries on. | `init.lua:2016`, `:2158` | Read |
| 3 | **Tether corrects along the wrong axis.** When a box's middle gets behind a surface, `GunTether` retracts the gun along wrist-to-box-middle, which is about along the barrel and so nearly parallel to a side wall. The next sweep then runs along that same line and may hit nothing, so it can repeat or pass. 63 tether events in the last native log. | `DebugView.cpp:744` | Read; effect inferred |
| 4 | **World meshes are surfaces, not solids.** Across the barrel a box is only a few cm, so it can end up wholly behind one triangle sheet with nothing left overlapping. Lengthwise the barrel keeps crossing the surface and is still detected. | `GunQueryInner`, near list | Inferred |
| 5 | **Starts at zero gap.** Sweeps set assume-no-initial-overlap (flag 16), yet the penetration fix leaves a box exactly touching, and the 2 mm skin is applied along the motion, so a grazing slide keeps almost none of it. A long flat side parallel to a wall is the worst case for that assumption. | `DebugView.cpp:787`, `:876` | Read; effect inferred |
| 6 | **Boxes turn with a stale rotation.** `weaponStop` uses `bone(h.index)`, last frame's solved pose. `g_VRHandRawRot` exists for this but is not used here. | `init.lua:1997`, `CharacterRig.cpp:2258` | Read |
| 7 | **Grip frozen from mixed clocks.** Until touching, the gun's offset in the hand is re-read from the weapon's world pose, the player frame and the bone, which may belong to different moments. It freezes at first contact with whatever error it had. Turning or strafing would make that error sideways. | `init.lua:2004` | Inferred |
| 8 | **Second contact can undo the first.** Each of the 3 slides removes only the newest normal, so a corner can push back into an earlier surface. | `DebugView.cpp:879` | Read |
| 9 | **Everything applies one frame late**, as in section 1. | | Read |

**Rule out before blaming the above:** light kinematic props (kind 10, up to 30 kg and 1.2 m) and NPCs do not stop the gun, by design and the user's earlier decisions. A report made against one of those is not this bug.

### Probe (goes in the same build as the fix, as the project works)

Per contact, log: muzzle arc from rotation this frame, `fix` from `GunQueryInner`, sweep distance, tether count, and the angle between `bone` and `g_VRHandRawRot`.
Test with a rifle held parallel to a wall: (a) swing by the wrist, (b) translate sideways, (c) walk alongside. If (a) fails and (b) holds, cause 1 is confirmed.

### Fix, in order

1. **Sweep the rotation.** Step from the last free orientation to the new one, in enough steps that the muzzle moves under about 1.5 cm a step, checking every step. At the measured 0.02-0.05 ms a move this is affordable, and quality goes up.
2. **Use `g_VRHandRawRot`**, with one fixed per-grip offset from a settled sample rather than re-capturing it each frame.
3. **Give the stop a rotation answer.** When a swing is blocked, limit the turn about the wrist, sharing correction between turn and translation. `g_VRHandRotLock` already carries a rotation into the arm solve. A partial limit needs a small extension.
4. **Tether along the surface normal**, and add a muzzle point to the middles it tests.
5. **Keep a real gap:** skin along the surface normal, and test whether dropping flag 16 is needed once a real gap exists.
6. **Constrain slides** against all planes of the move (crease handling), as a character controller does.

## 3. Threads: what is actually slow

### Measured

| Item | Value |
|---|---|
| All Lua on the main thread, per tick | mean about 5.5 ms in normal windows, p99 11-28 ms, max 47 ms outside loading |
| HandCollision share | 1.6-2.4 ms a tick |
| The sweeps themselves, in C++ | 0.014-0.05 ms a call |
| World lists, native, called from Lua | 1.28 ms a frame mean, 2.2 ms max |
| Worst one-off sections | `pointing` 72, `marks` 62, `gun before` 60, `taking` 60, `aim cone` 47, `world rays` 40 ms |
| Collision debug view (off in normal play) | about 4 ms a tick |
| Effect of Lua on frame time (2026-10-09) | adding 2.5 ms of Lua cost only 0.7 ms of frame: the engine overlaps it |

### What this means

- "Only in Lua" is not quite right. PhysX simulates on its own, and the geometry queries are already C++. Lua is the orchestrator, paying for hundreds of native calls, `Vector4` userdata and garbage collection per frame.
- Lua itself **cannot** be spread over threads: one Lua state, and game object calls belong on the main thread.
- A worker thread alone does not shorten a frame if the frame still waits for its answer, and it can add a frame of delay if it does not. Placement decides latency.
- The spikes are mostly specific operations and not steady physics load. `gun before` contains the world-list work, whose 60 ms outlier needs its own explanation. It may be the rebuild after `GunForget`, or the slow game mass lookup in the kinematic cache.

**Worth noting:** `onUpdate` is documented as running twice a frame (2 ms and 21 ms apart), but the logs show 41-45 ticks a second at 45 real fps, so about once. `rec.since` guards either case. Settle this during the first probe.

### Target layout

| Where | Work | Why there |
|---|---|---|
| **Lua, main thread** | Decisions and events only: start and end of a grip, settings, weapon change, sounds. No per-frame physics. | What Lua is good at; removes most marshalling and garbage |
| **Animation hook (worker)** | Gun and hand movers against the snapshot, applied in the same pass. | Removes a frame of delay; each solve is about 0.05 ms |
| **Just before `simulate`** | Carry drive, spin, reading the body back. | Only safe, freshest moment to write to bodies |
| **Snapshot step** | Near lists and moving obstacles, published by atomic swap. | Slowest repeating task; about 1.3 ms off the main thread |
| **Low-priority worker** | Grid index of statics, per-actor mass decisions. | Removes the rescans and slow game calls from the frame |

### Hazards that must be solved first

- **Today's mover state is not thread-safe.** `gunNear`, `movers[]`, `GunForget` and friends are unsynchronized globals. They need one owner each, or an immutable snapshot.
- **Bone snapshots can tear.** `g_VRFKSnapPos` and `Rot` are `volatile` arrays with no sequence lock, and `HeldPicture` already reads them. Add a seqlock or double buffer. Animation passes run several times a frame, so choose which pass counts.
- **Actor and mesh lifetime.** Near-list copies hold mesh pointers that live as long as their sector. Streaming must not be able to free them under a worker: define a safe window from the probe, or pin the sector.
- **PhysX access windows.** The walk of actors must not overlap `simulate` or `fetchResults`. Whether the game uses `lockRead` is unknown. The notes also warn not to rely on `__try` around guessed pointers in this process.
- **The Lua-to-native boundary moves.** `init.lua` is already at the 200-local limit, which is another reason to shrink it. `carryDrive` also feeds the throw, sounds and hand-on-object drawing, so these need a read-only result channel back.

## 4. Suggested order of work

| Phase | Work | Check it worked | Size (rough) |
|---|---|---|---|
| 0 | Probes: `simulate` timing and thread, gun side-on log, the 60 ms `gun before`, tick count. | Unknowns above answered; no behaviour change | Small |
| 1 | weaponStop fixes 1, 2, 4, 5, 6. | Wall test passes; `gun move` stays under 0.2 ms | Medium |
| 2 | Native carry before `simulate`, deadbeat gain. | Repeat the `[trace]`: median residual under 10-15 mm, delay 0 frames | Medium |
| 3 | Rotation stop in the arm (weaponStop fix 3). | Barrel stops at the wall and the hand does not slide; no shake | Medium |
| 4 | Movers into the animation hook, snapshot and seqlocks. | Frame delay gone; `[lua]` HandCollision well under 1 ms | Large |
| 5 | Off-thread world lists, grid index. | No world-list time on the main thread; spikes gone from `[cost]` | Large, highest risk |

Phases 1-3 answer the two complaints players can feel. Phases 4-5 answer the architecture one, and only after 0 has established the safe windows.
All of it respects the existing rule that collision quality is not traded for speed.

## Not established

- The exact stage and thread `simulate` runs on, and whether it is substepped.
- Causes 1, 3, 4, 5 and 7 are consistent with the code but not yet seen in a per-frame capture.
- Whether the original report meant props, corpses or both. Corpses (kind 12) have no picture at all, so they lag visibly regardless.
- The improvement figures are estimates until phase 2's trace is repeated.
