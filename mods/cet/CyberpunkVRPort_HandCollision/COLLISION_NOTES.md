# Hand and gun collision: what is known, what was tried

Findings from the work of 2026-11-07/08 (commits `4aadac60`, `e800b4e8` on `hand-interaction`). Read this before changing
how the hands or the gun meet the world: most of the obvious ideas have been tried, measured, and some of them failed.
Every number here was measured in game on the reference PC (Ryzen 5 5600X, RTX 4080, Valve Index at 90 Hz, DLSS-G
pacing the game to 45 real fps). That PC is **CPU-bound**: every millisecond on the game thread costs frames.

## How it works now

| Piece | Where | What it does |
|---|---|---|
| World lists | `src/Natives/DebugView.cpp`, `VRWorldCenter` | Once a frame, round the chest (2.8 m): the **static near list** (built a slice of 1500 statics per frame, rebuilt after 1 m of movement or 2 s) and the **moving things** (gathered every frame). |
| Movers | `DebugView.cpp`, `struct Mover`, `GunMove` | The gun (`movers[2]`) and each hand (`movers[0..1]`). Parts are boxes or capsules in the body's frame. Each frame the body is **swept** from where it was left to where the solve wants it, stopped and slid along what it hits (PhysX `PxGeometryQuery::sweep`), pushed out of what it is in (`computePenetration`), and **tethered**: nothing may lie between it and its anchor (gun: the wrist; hand: the chest, ignoring the first 12 cm). |
| Moving obstacles | `GunTouchActor`, `GunStopsAt` | A moving actor that is **kinematic or heavier than 30 kg** is an obstacle like a wall. Lighter ones are **shoved** (Lua `bodyPush`) from the touches after the move. **Query-only shapes are skipped** (see the NPC hull below). The moving actor whose bounds hold the chest is skipped (the player's own capsule, a seat). |
| Gun shape | `weapon_shapes.lua` (generated), `weapon_custom_boxes.lua` (hand-drawn, wins) | Boxes in the weapon frame: origin at the grip, +Y along the barrel, +Z up, metres. Turned boxes allowed. |
| Attachments | `weapon_custom_boxes.lua` `attachments`, `weapon_attachments.lua` | Drawn once per attachment in **its mount's frame**; put on at the weapon's `muzzle` / `scope` mount. One mount per slot per weapon, never per attachment (user's decision). |
| Hand shape | `init.lua` `K.handWorldOne` | The body asset's hand capsule (`capsules.lua`, r 0.031) plus a finger capsule (centre 0.115 along X, r 0.016, half 0.03) that ends ~1 cm short of the fingertips, so touch screens still work. |
| Weapon reads | `init.lua` `WE` | Slots, outline and Silverhand capsules read **on game events** (draw, part added/removed via `ObserveAfter`), plus late looks at 0.3/0.7/1.5 s and a 10 s safety read. Reload is only counted: following the slide is not worth it (user's call). |
| Rays | `init.lua` `K.gunWalls`, `worldRay` | Now only a fallback/compare mode. Overlay: "Gun: backup rays from the wrist" (default **off**), "Hands: old world rays instead of their shapes" (default **off**). The hands also use the rays for the few frames after a load while the near list is not built yet. |

Costs (no debug view): world lists 0.6-1.2 ms/frame, each hand sweep 0.01-0.06 ms, gun move ~0.02-0.05 ms, weapon
reads 0.15-0.2 ms. The **debug view costs 1.3-3.7 ms** by itself: never judge performance with it on.

## Bugs found, and how

- **The static near list was empty from the start** (since `2b965ff5`, which introduced it). `GunAddActor` called `PxGeometryQuery::getWorldBounds(geom, pose, 0.f)`.
  The third argument **scales the extents** (1.0 = true size), so every shape became a point and only shapes whose middle
  lay within reach were kept: 0-4 shapes in a city. The gun's boxes collided with almost nothing; the wrist rays did all
  the stopping. Found with a probe that walked every scene at the point a ray hit (`VRGunProbeAt`), which showed the
  near shapes had bounds `0.0 x 0.0 x 0.0`. Fixed to `1.f`.
- **The gun stopped a hand's width off every NPC** once moving obstacles were added: NPCs are kinematic and carry a big
  **query-only convex** (shape flags 10 = scene query + visualization, no simulation; query word2 `08d00010`, sim word0
  `00018000`). Everything solid has flags 11 (simulation + query). Moving obstacles now need the simulation flag.
- **All hand collision died** after the hands change: `local K` lives inside `local HC = (function() ... return {...} end)()`
  and `registerForEvent('onUpdate', ...)` after it cannot see `K` or any inner local (`WORLD_HIT`, `pushOn`, ...). Every
  frame threw `attempt to index global 'K'`, which stopped the whole update. `K` is now in HC's return table and onUpdate
  uses `HC.K.*`. **Before installing new onUpdate code, scan it for inner-only locals** (a short Python scope scan was used:
  collect `local` names at the top of the HC function, check which are used after `registerForEvent('onUpdate'`).
- A **failed build still ran the install script** once (it reinstalled the old DLL, verified by file compare). Build and
  install only in one command with an exit-code check.

- **Arms moved by themselves** once a hand was out past ~50 cm (2026-11-08). `[hand-probe]` showed pushes of 10-36 cm in free air, turning
  every sample, 15 static shapes unchanged, the tether hitting at about the wrist's own distance, and `moving that stop` going 0 -> 1 -> 2 with
  the arms involved. The obstacle was a capsule of 0.13 x 0.11 x 0.17 m, flags 11, **all query and sim words zero**: very likely the player's own
  body capsule following the hand. Inferred, not proved: arm and hand are one actor, so the "bounds hold the chest" skip lets it through only once
  the arm is out. Moving shapes with all filter words zero are no longer obstacles. Check `own capsules skipped` in the `[world]` line.

- **The gun stopped at the player's own capsule**: a moving upright capsule beside the chest (0.12 x 0.12 x 1.72 m, query word2 `48000002`, sim word0 `2`; also seen
  0.40 x 0.40 x 1.44 m). Moving capsules taller than 1 m and under 0.6 m across are no longer obstacles. Crouched, the 0.40 m one is 0.80 m tall with the same words (query word2 `00002000`, sim word0 `1`, sim word2 `41ce`), so it is very likely the player's; capsules with those words are skipped at any height.

- **Palms have big round colliders.** After touching a palm the log named one round static kind: a sphere 2.15 m across, flags 11, query word2 `00100000`, sim words zero.
  Exactly that kind (sphere + those words) is left out of the near list. Identified from one test run by shape and timing, not by material: if something else stops being solid, look here first.

## PhysX facts (PhysX 3.4.2, `PhysX3_x64.dll` of the game)

- Scene vtable at RVA `0x20CAF0`: slot 94 raycast, 95 sweep, 96 overlap.
- **Scene overlap answers 0 hits with every filter** (zero words, the game's preset words 2/3, all ones, blocking) while
  12-50 static actors reach the sphere by bounds; it takes 0.005-0.1 ms, so it searches an empty or other structure.
  Abandoned. Walk the actors instead.
- `NpRigidDynamic` vtable at RVA `0x20b2e8` (found by `getType` returning 1, no RTTI COLs in this DLL):
  slot 31 `setMass`, **32 `getMass`** (RVA `0x4dce0`, returns `1/invMass`, **0 for kinematic**), 33 `getInvMass`,
  **47 `getRigidBodyFlags`** (RVA `0x4ded0`, writes the flags byte to the out pointer; bit 0 = kinematic). The plugin
  proves both slots by address on the first moving actor and never calls a table that is not that one.
- Actor slots used: 5 getType, 9 getWorldBounds, 20 getGlobalPose, 26 getShapes. Shape slots and their RVAs are proved in
  `GunProveShape`.
- A sweep hit's normal is not at the header offset; it is searched once (`gunHitNormalAt`, found at +40).
- Static actors: ~12-21k per owned scene; one actor can hold a whole 20-60 m stretch as a triangle mesh, so 10-70 near
  shapes is normal. Moving actors: ~1600-2750; only 4-90 within 4 m. Asking each actor where it stands is the cost, not
  its bounds.
- 8 scenes exist; only one has a game owner (the origin offset at owner `+0x134`). The others hold planes with infinite
  bounds and were never needed.
- Things seen that stop the gun but are not static: a 127 kg kinematic counter/table (`kind 10` in the game's body info),
  doors, vehicles. Objects with **no game body** exist among the moving actors too.

## Tried and rejected (do not redo without a new reason)

| Idea | Result |
|---|---|
| Skip collision every other frame to save time | Rejected by the user: collision quality must not drop. |
| PhysX scene overlap instead of walking actors | 0 hits, see above. |
| Cache static bounds per actor | Slower (0.68 vs 0.55 ms/call): touching each actor is the cost, not the bounds. |
| Rays only for the gun (wrist to box middles/fronts) | Lines miss edges and rails; they only say "free until here". Kept only as a compare switch. |
| One outline box per weapon | Mostly air (a pistol is an L in a rectangle). Generated multi-box shapes, then hand-drawn ones. |
| An offset per attachment | Rejected by the user: attachments go exactly on the weapon's mount; only the mount is moved. |
| WolvenKit CLI for mesh export | WolvenKit 9.1 CLI needs .NET 10. Meshes are decoded straight from the serialized `.mesh.json` render buffers instead (`tools/weapon_blend/make_weapon_blend.py`). Since 2026-11-08 the CLI runs with the local runtime: `E:\cyberpunk_vr_port\tools\wk.sh` (sets `DOTNET_ROOT` to `tools\dotnet10`); used for listing archives and extracting `.app` files. |
| A collider component on the weapon in WolvenKit | The game has no held-weapon collider (of 943 firearm meshes one has physics, a magazine box), and the mod would not read it. |

## The Blender workflow for gun boxes

1. `blender -b --python tools/weapon_blend/make_weapon_blend.py -- <_meshj dir> <_appj dir> <out.blend> w_handgun__tsunami_nue`
   builds or **adds to** a `.blend` (existing boxes and moved mounts are kept). Parts not in the default appearance
   (Jackie's compensator) and `_rail` parts go into a hidden `<family>/optional` collection: they must not decide the mounts.
2. The user edits: mesh objects named `box*` (move, turn, scale; no shear), the empties `<family>/muzzle` and
   `<family>/scope`. Attachment mount empties are locked on purpose.
3. `blender -b <file.blend> --python tools/weapon_blend/read_weapon_blend.py -- mods/cet/CyberpunkVRPort_HandCollision/weapon_custom_boxes.lua`
   writes `{cx, cy, cz, hx, hy, hz, qi, qj, qk, qr}` boxes. It computes world matrices itself: `matrix_world` is stale for
   objects in hidden collections when Blender runs headless.
4. Close Blender before a script changes the `.blend`, or the open session overwrites it on the next save.
5. **The other 56 firearms** are in `E:\cyberpunk_vr_port\blender\weapons_boxes.blend` (2026-11-08), built with
   `--ref-attachments mods/cet/CyberpunkVRPort_HandCollision/weapon_custom_boxes.lua` and the appearances in `_appj_all`: the
   attachments there are locked references (`attachment_ref`, not read back), so the attachments drawn in `nue_boxes.blend` stay.
   Read both files back in turn; tested on a copy: 57 weapons, Nue and all 30 attachments unchanged. Only the first weapon is shown;
   the text block `attachments_on_weapon.py` hangs the attachments on the weapon whose collection is active. Starting boxes are the
   generated ones and miss borrowed parts (e.g. the Sidewinder's stock).
6. **Appearances:** every firearm `.app` was extracted with the WolvenKit CLI (`archive -l -r`, `extract -r '\\weapons\\firearms\\.*\.app$'`,
   `convert serialize`) to `_apps_all` / `_appj_all` (311 files). The weapon's own `<family>.app` default is the whole weapon (body,
   receiver, stock, mags); the part apps (`__base1_01`, `__rcv1_01`) hold one part each and are named unevenly (`__base1`, `__base_01`,
   `kang_tao`, `submachinegun`), so `__base1_01.app` alone wrongly made receivers optional. **Some weapons borrow parts** with no
   offset (skinned, same frame): Sidewinder = Copperhead receiver + stock, Igla/Palica/Testera = Satara receiver, Ashura = Nekomata
   receiver. They are loaded as `<mesh>@<weapon>`. No own app: Lerus Teaseher, Techtronika Uragan (all their meshes are shown; their
   frames look odd, check them). Tamayura's appearance is inside the Nue's app (`tamayura_default`).
7. Mesh JSONs: `E:\cyberpunk_vr_port\tools\extract\_meshj`, appearances `_appj`. Vertices are `PT_Short4N` positions in
   stream 0, quantized by the header's scale/offset; indices are `IBCT_IndexUShort` at `indexBufferOffset + teOffset`.
   Skinned weapon meshes are in bind pose in the weapon frame. A magazine mesh is **not** in the weapon frame.

## Testing without the game

- `build\luajit\host\minilua\Release\minilua.exe` compiles Lua but has no `print`, `math`, `tostring` or `loadfile`:
  use it only as a syntax check (`io.open` + `loadstring` + `io.write`).
- For logic, run the functions in Python with `lupa` (`pip install --user lupa`): cut the function out of `init.lua`,
  mock `Game`, `Vector4`, `spdlog` and the natives, and call it.
- Disassembly: `pefile` + `capstone` on the game's DLLs (no `-I` flag for Python, the packages are in user site).

## Diagnostics left in

- `[gunshape] ...` every 10 s while a gun is out: boxes, near shapes, ms per call, rescans, moving things that stop/are shoved.
- `[gunshape] a static/moving thing stopped/pushed out a hand or the gun: geometry, flags, bounds, filter words` once per kind of shape.
- `[world] round the chest: near shapes, moving, own capsules skipped, ms a frame, hands calls/ms` every 10 s.
- `[hand-probe]` when a hand is pushed > 10 cm, the push reverses from the last frame, or the chest tether fires.
- `[HandColl] probe: static ray ...` / `[gun-probe]` only while the gun's backup rays are on (the scene walk is disabled: it cost 2.5 ms).
- `VROverlapProbe` and `VRGunProbeAt` natives are still registered but unused by default.

## Open

- Whether the zero-filter skip ends the arm fault still has to be confirmed in game, and why it does not happen in every session is unknown.
- **Weapons with borrowed parts got another weapon's boxes** (fixed 2026-11-08, found from the `.app` component order, not yet tested in
  game): `weaponBoxOf` took the family from the last component with a known weapon's name; Igla and Testera end on Satara parts, Unity on
  Liberty's rail, some Sidewinder and Umbra skins on Copperhead and Masamune parts. Now the family with the highest score wins (body part
  `__base` without `_rail` 100, anything else 1). Simulated on the component names of 452 appearances of the whole-weapon apps, also in
  reverse order. "First `__base` wins" was not enough: the Tactician holds an Ashura `__base` piece, Umbra's neon skins a Masamune one.
- The finger capsule is straight: a fist stops a few cm early against a flat wall.
- Magazines have no box of their own (inside the grip they are covered; in the hand the hand capsule covers them).
- Limits: 12 parts per body (`kGunMostBoxes`), 1024 tests per call (`kGunMostTests`), 14 ray targets (`GUN_SIDES`).
- The world lists cost 0.6-1.2 ms/frame even with no gun out. A grid index of the statics would cut the rebuilds.

## How the user works

- Verify before deciding; probes go into the same build as the change they test.
- No code change without the user's approval; install only with the game closed and a verified backup
  (`E:\cyberpunk_vr_port\install_gun_turned.ps1` backs up and installs the DLL, `init.lua` and `weapon_custom_boxes.lua`).
- Nothing is committed or pushed without the user's go-ahead. Branch `hand-interaction`, remote `fork` (EffinexX/cyberpunk-vr-port).
- Build: `$env:CMAKE_GENERATOR_INSTANCE='E:/Programs/VSBuildTools'; cmake --build build --config Release --target cyberpunkvrport_stereo`
  (incremental ~0.2 min).
