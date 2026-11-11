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
| Moving obstacles | `GunTouchActor`, `GunStopsAt`, `GunKinematicIsLoose` | A moving actor that is **kinematic or heavier than 30 kg** is an obstacle like a wall, except a kinematic prop (kind 10) of at most 30 kg game mass and 1.2 m longest side. Lighter ones and those props are **shoved** (Lua `bodyPush`, which wakes a kinematic one) from the touches after the move. **Query-only shapes are skipped** (see the NPC hull below). The moving actor whose bounds hold the chest is skipped (the player's own capsule, a seat). |
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
  **The same standing words round an NPC** (reported 2026-11-08, hand and gun stopped at a yellow capsule round one): the log named a moving capsule 0.80 x 0.80 x 2.00 m, flags 11, query word2 `48000002`, sim word0 `2`, sim word2 `dcc`. Too wide for the 0.6 m test, so it stopped them, and hands were pushed up to 0.7 m (`[hand-probe]`). That it was this capsule is inferred from size, words and the report. Capsules with those two words are now skipped at any size; the user confirmed it in game the same day.

- **Palms have big round colliders.** After touching a palm the log named one round static kind: a sphere 2.15 m across, flags 11, query word2 `00100000`, sim words zero.
  Exactly that kind (sphere + those words) is left out of the near list. Identified from one test run by shape and timing, not by material: if something else stops being solid, look here first.

- **Small things stopped moving under the gun** since `4aadac60` (reported 2026-11-08): kinematic actors became obstacles and never reached `bodyPush`/`bodyWake`, which is what made them dynamic; a thing that was woken also goes back to kinematic once it lies still. PhysX `getMass` is 0 for kinematic, so `GunKinematicIsLoose` asks the game's mass (`BodyGameMass`, decision kept per actor). Confirmed in game 2026-11-08 (user: small things move again). Of the 60 logged kinematic bodies near, 52 were kind 10 at 0.09-19.75 kg and 0.10-0.90 m, all shoved, so light kinematic props are common; the rest were kinds 1, 2, 3 without a game mass and stayed obstacles. No kind 10 body over the 30 kg or 1.2 m limits was among those 60, so the log does not show those limits at work. The per-shape `bounds` in the "stopped a hand or the gun" lines say nothing about the size of the actor.

- **Skippy made gun and hand jump** (reported 2026-11-11, `w_handgun__arasaka_yukimura`, drawn 16:48). Counted over the HandCollision log per drawn weapon:
  35 `[contact] gun, moving` in under 2 minutes, at 10-34 m/s, always `metal.physmat (guess, ray nil)`; every other weapon logged since 2026-11-09 had 0-8 in a
  whole session. So something moving stopped the gun again and again. **Very likely a body of Skippy's own that follows the weapon** (pushed out, it
  followed, again); inferred from the counts, not proved. The native log names two first-seen moving shapes of that session that may be it (once per kind, no
  time): a sphere 0.20 m, flags 11, query word2 `00400000`, all else zero ("pushed out"), and a box 0.75 x 0.75 x 0.61 m, query word2 `08104040`, sim
  `00004040 0 000002fd 80000000` ("stopped").
  **Second session (16:59, with `FollowsHeld` in): the jumping went on** (user). `FollowsHeld` never fired (no `follows a held one` line, 0 in `[world]`); very
  likely because it compares with the solved gun, which is the one being pushed, and the hands are not solved natively in this build (`hands 0 calls`). Only 2
  `[contact]` lines this time, but the 0.20 m sphere above was "pushed out" right at the draw again (it did so at the draw in the first session too). So the
  jumping is most likely the gun pushed out of that sphere each frame (a push-out makes no `[contact]`). Since 17:02 (installed, **not yet tested**) moving
  spheres with exactly those words (query word2 `00400000`, all other words zero) are no obstacle, and `[gunshape] Skippy's sphere?` logs every 2 s where it
  lies against the gun (in the gun's frame) and the hands, with `FollowsHeld`'s count for it.
  **Confirmed 2026-11-11 17:04-17:14** (user: seems to work now; 0 `[contact] gun, moving` with Skippy out). The probe measured the sphere, radius 0.100 m,
  **centred on the gun's grip origin**: 0-4 cm from the solved gun (mostly under 2 cm, up to 18 cm while the gun was being stopped), 4-11 cm from the right
  hand, in all 60 lines. So the gun's boxes always lay inside it: proved, not inferred any more. A new actor (proxy 66828, then 71646) comes with each draw.
  `FollowsHeld` did catch it once, late (63 with it, 21 not); on the second actor it counted 23 with, 191 not, because the gun it is compared with jumps while
  it is pushed. So the word rule is what fixed it. `FollowsHeld` also fired on hand-sized capsules with all words zero (the player's own, already left out
  by the zero-word rule): harmless. Not known: whether other weapons carry the same sphere (the logs of 2026-11-09 had few moving contacts with other guns).
  First fix, still in: `FollowsHeld` in `DebugView.cpp` -- a moving actor
  that would stop them, under 1 m, within 0.5 m of the gun or a hand, that moves as that one does relative to the chest (4 matches, at least 3 per miss, a match
  within 35 % of the held thing's move of over 2 cm) is no obstacle from then on (kept per actor until unseen for 2 s). Loose things are left out of the rule, so
  what the gun shoves goes on being shoved. Read `[gunshape] a moving thing follows a held one` and its `its shape` lines to learn what Skippy carries, and
  `following a held thing` in the `[world]` line.

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
8. **Melee, 29 families** (2026-11-08): `E:\cyberpunk_vr_port\blender\melee_boxes.blend`, read back into the same `weapon_custom_boxes.lua`
   (86 weapons then). Meshes and apps in `tools\extract\_melee_flat` (`mesh`, `app`). In that frame **the blade runs along +Z**, the origin at
   the grip. No mounts. Variants share a family, so one set of boxes covers them (6 katanas, machete/borg/axe). The crowbar's components are
   `w_blade_long__crowbar__...` though its meshes are `w_club_1h__crowbar` (`K.MELEE_ALIAS`); the shovel's are `w_shovel__grip...`, with one
   `__` only. Cane (`i1_001_ma_slot__fingers_cane`, placed by its `.app`: quarter turn, +0.055 Y, +0.6 Z) and dildo (`sextoy_a_dildo_c`; family named `w_club_1h__dildo` in the `.blend`, its component has no `w_` name: `K.MELEE_ALIAS`) were added afterwards. **Open:** parts of a mech and shields stop the blade (moving shapes in the native list, not the character capsules), so it hardly reaches the body. `w_knife__tw3` is named in an appearance, but its mesh is in no archive. Shovel's mesh starts at +0.18 m on Z: check in game.
   **Behaviour (user's decision, 2026-11-08):** the boxes appear and work in game. Freeing the blade by its tip's speed (2 m/s, 0.35 s) was tried and
   switched off far too often (a turn of the wrist is enough), so it is gone: the world always stops a melee weapon, in a stroke too. People do not
   (their capsules and query-only shapes are left out natively); whether a mech does is not known.
   **A blow moves what it struck:** `VRMeleeSweepHit` notes each landed hit (`VRMeleeLastHit*`), `K.meleeHitTick` moves the part nearest it along the
   blade's way and back (0.07 s out, 0.28 s back, 6-20 cm by speed) through `VRNpcGrip`, in the holding hand's slot, from where the part stood when
   struck: a slot read later already has the offset in it. On top of the game's damage. Humans' rig only. **Not yet tested in game.**

## Testing without the game

- `build\luajit\host\minilua\Release\minilua.exe` compiles Lua but has no `print`, `math`, `tostring` or `loadfile`:
  use it only as a syntax check (`io.open` + `loadstring` + `io.write`).
- For logic, run the functions in Python with `lupa` (`pip install --user lupa`): cut the function out of `init.lua`,
  mock `Game`, `Vector4`, `spdlog` and the natives, and call it.
- Disassembly: `pefile` + `capstone` on the game's DLLs (no `-I` flag for Python, the packages are in user site).

## Finding a regression: reading the logs

Learned from the "small things no longer move" fault (2026-11-08, fixed in `c5bd5b04`).

- **Logs of the last session**, under `<game>\red4ext\plugins\cyberpunkvrport\` (game: `E:\SteamLibrary\steamapps\common\Cyberpunk 2077`):
  `log\cyberpunkvrport.log` holds the native `[gunshape]` / `[world]` lines and covers one session. `lua\CyberpunkVRPort_HandCollision\CyberpunkVRPort_HandCollision.log`
  holds the `[HandColl]` lines and spans several sessions, so filter by time.
- **Count over the whole log before stating a pattern.** A first look at a few lines suggested "nothing is ever shoved"; over the whole log 25 of 36 lines had shoved > 0.
- **`bounds` in "stopped a hand or the gun" are one shape's**, not the actor's: a 18 cm shape can belong to something large.
- **`[HandColl] probe: the gun's boxes are in body ...` only names what is already shoved.** What stops the gun never gets that far, so a probe full of `kinematic 0` says nothing about the bodies that stop it.
- **Two views of "kinematic":** the PhysX flag (`GunStopsAt`) and the game's `VRBodyIsKinematic`. PhysX `getMass` is 0 for kinematic; the game's `GetMass` is not (`BodyGameMass`).
- **Proxy kinds** (`ProxyKind`, `TraceMoves` in `HandBody.cpp`): 1 physical system, 3 destruction, 10 aggregate (loose props, also the 127 kg counter), 12 ragdoll. `bodyWake` never wakes kind 1. Kinds 1, 2 and 3 near the player had no game mass.
- **Resting state is not permanent:** a body `bodyWake` turned dynamic goes back to kinematic after `K.SETTLE_FOR` of lying still (`rec.checked` is cleared, so it can be woken again). Whatever decides "obstacle or shoved" must hold for both states.
- **Any new rule on which bodies stop the gun also applies to the hands:** they share `GunTouchActor`. Test both.

## Diagnostics left in

- `[gunshape] ...` every 10 s while a gun is out: boxes, near shapes, ms per call, rescans, moving things that stop/are shoved.
- `[gunshape] a static/moving thing stopped/pushed out a hand or the gun: geometry, flags, bounds, filter words` once per kind of shape.
- `[gunshape] a kinematic body near: kind, game mass, longest side, proxy: shoved/stops`, once per actor, 60 lines a session.
- `[world] round the chest: near shapes, moving, own capsules skipped, ms a frame, hands calls/ms` every 10 s.
- `[hand-probe]` when a hand is pushed > 10 cm, the push reverses from the last frame, or the chest tether fires.
- `[HandColl] probe: static ray ...` / `[gun-probe]` only while the gun's backup rays are on (the scene walk is disabled: it cost 2.5 ms).
- `VROverlapProbe` and `VRGunProbeAt` natives are still registered but unused by default.

## Contact sounds (2026-11-08)

- `contact_sounds.lua` plays a sound when the gun or a hand newly meets the world; `VRMoverContact(id, what)` (`DebugView.cpp`) hands it what the last sweep hit: point, normal, moves ago, speed towards the surface.
- **The boxes do not know the material**, and the backup rays are off by default. So one ray at the contact point, along the normal, asks the game for the name (`tr.material`): up to 3 tries, each static then dynamic, only on a new contact. A miss plays the concrete sound and counts in the log.
- Reading the material from PhysX directly was considered and left for later (user's decision): it needs a kept shape pointer, new unproved shape slots, the face index and the game's mapping to names. Worth it only if the `[contact]` log shows the ray missing often.
- The sweep hit's position is taken as the three floats before the normal (`gunHitNormalAt - 12`, as in `PxLocationHit`). **Not proved in game**: it is believed only near the part that stopped, else the part's middle is used (`[contact] ... point part middle`).
- What sounds: sweeps (`VRMoverContact`, world and moving obstacles) and, since shoved things stop no sweep, new touches of loose things (`K.contactLoose`, `VRGunTouchAt`; 2026-11-08, not yet heard). The first frame after a jump (`GunQueryInner`) and the chest/wrist tether make no sound. On something moving the dynamic ray goes first, over all presets; a miss or a `concrete`-group answer within 0.8 m of an NPC counts as flesh. In the log of 2026-11-08 no `character_*` material ever came back, so whether rays reach NPCs at all is unmeasured.
- Sounds come from `base\sound\metadata\cooked_metadata.audio_metadata` (`audioPhysicalMaterialSettings`, `physical_material_to_audio_metadata_matrix`) and `base\sound\event\eventsmetadata.json`, all names checked against the latter. `ph_metal_generic_hard` lasts 3-4 s: not used. Hands use the `lcm_fs_additional_*_hand_grab` events (0.1-1 s).
- Measured in game 2026-11-08 (`[contact]` log, 150 contacts): about 95 % found a material, about 70 % of the points were PhysX's own (so the position offset works). The user heard the sounds, and the weapon knock drowned the objects at full Wwise level.
- Loudness by Wwise parameters alone does not reach the gun knocks, and the port's own voices (Ogg files from the banks through `VRMenuPlayWav`, panned) were tried first and dropped 2026-11-08 for lack of position and reverb. The sound effects are Opus behind `sfx_container.opusinfo` (`tools/vr_lobby_assets/game_sounds.py` reads them).
- Sliders: "Touch sounds: objects" / "weapon" (overlay and VR menu, `xr_touch_object_volume`, `xr_touch_weapon_volume`, defaults 80 / 30 %), read by `VRContactVolume`.
- User's by-ear decisions, 2026-11-08: of the gun knocks only file 7 of each (`M.ONLY_FILE`): the others clink like glass, and pistol and rifle sound alike. Sliders settled at objects 50 %, weapon 3 %: now the defaults.
- Wwise route, investigated 2026-11-08 from the banks and the exe, **nothing of it tried in game**. `Cyberpunk2077.exe` **exports** 499 symbols, among them `AK::SoundEngine::RegisterGameObj`, `SetPosition`, `PostEvent`, `SetSwitch`, `SetGameObjectOutputBusVolume(emitter, listener, gain)`, `SetGameObjectAuxSendValues`, `SetListeners`, by their mangled names: so `GetProcAddress` reaches them, no signature hunt. Volume curves on the hierarchy, input to gain: `ph_impact_force` 0 -> silent, 5.6 -> 0.51, 19.8 -> 0.75, 100 -> 1 (all `ph_*` used here); `lcm_foley_volume` is +-12 dB (all hand grabs); `RTPC_w_melee_volume` +-30 dB (`w_melee_*`). `w_gun_*_melee_hit` have no named volume parameter. The earlier search for parameters by name, in this file, only looked for six names and missed `lcm_foley_volume`. Those three gun knocks share the same 30 sources behind a switch group `w_melee_material_light` (states cloth, concrete, glass, implant, metal, skin, wood; 5 each): of the 8 files made by `make_sounds.py`, 1 is glass, 2/6/8 wood, 3/7 skin or implant, 4 concrete, 5 metal. So the file the user picked by ear (7) is the knock on skin, and the clinking was the glass and other surfaces.
- The Wwise route, built 2026-11-08 (`Audio/WwiseEmitter.cpp`, `VRWwisePlay`; tested 2026-11-08, user: all seems to work; log: 720 of 720 sounds through Wwise, every call result 1, listener 667 found after the first few plays, gain with the listener -1 also returned success, no faults. The `[contact]` log is sampled, and its sample held no `loose` or `person near` line, so those two rest on the user's impression only). Four emitters of ours in turn; `AkWorldTransform` read from the game's own caller at RVA `0xa172b4`: front and top floats, position doubles at +24, each vector (x, z, y) of the game's, flags 3. Its `[wwise]` lines in `log\cyberpunkvrport.log` give each call's result (1 is success) and the listener. If Wwise does not start it, the event plays on the entity at the game's own loudness. Open: no game reverb or occlusion is set on these emitters.
- Melee weapons need nothing of their own: they run through `weaponStop` and so `K.contactGun`. Logged 2026-11-08 with the 18:10 build: `w_melee_blunt_impact_*` played for iron pipe and sledgehammer. In that sampled log the material ray missed mostly on `moving` (11 of 15) and `loose` (11 of 28, with a part's middle as start), against 7 of 153 on `world`; a miss plays the concrete sound. Cause not looked into; a long part's middle may lie far from the touch.
- After those misses (2026-11-08, not yet heard): a loose thing is first asked by a ray from the part straight at the body's own middle (`VRBodyGetPos`), and something moving also by a ray with neither the static nor the dynamic filter. Still no answer: loose sounds as plastic, moving as metal (`guess` in the log, counted as missed), a guess at what such things mostly are. Whether these rays hit more often is not measured yet: the `[contact]` lines now name the body's kind and mass.
- People had almost no sound (reported 2026-11-08): they are stopped by the Lua capsule solve (`resolveRig`, label `npc`), not by the sweeps, so the sweeps and the near-NPC test only rarely met one (2 `person near` lines in the log). `resolveRig` now notes each first touch (`PERSON_TOUCHED`), `K.contactPeople` plays it as flesh, no ray. Ranged weapons and hands only: a melee weapon is not solved against people, and its blow has the game's own hit sound. Heard 2026-11-08: people sound, and loose things too (user). A scrape on a person was loud with the gun and nearly silent with the hand: very likely the gun's came from that capsule through the sweeps (another event), the hand's from `M.personSlide`'s cloth; so a person's scrape got its own level, `SCRAPE_PERSON`, 0.6. A scrape along one comes from the same solve (`PERSON_PRESSED`, `M.personSlide`). A loose thing's touch plays at its first frame fast enough, not only at its first frame; a failure in `K.contactLoose` is said once in the log.
- By ear 2026-11-08: the scrape was too loud, started on short knocks, and did not seem to come from a place. `eventsmetadata` `maxAttenuation`: the player's `lcm_fs_additional_*` 0 (no attenuation), the NPCs' `lcm_fs_npc_additional_*` 30, so slides and hand grabs now use the NPCs'; there is no NPC slide on plastic (linoleum instead). `w_gun_*_melee_hit` are also 0. A scrape now needs 0.25 s of contact and is at 20 % of the objects slider. Confirmed by ear the same day (user: now it sounds right).
- Scraping (2026-11-08): `ph_*_scrape` exist for few materials, so the `*_slide` events (`lcm_fs_npc_additional_*`) (one per surface, 1.4-1.9 s, not looping) is started again every 1.1 s on two emitters in turn and stopped by `ExecuteActionOnEvent` with a 150 ms fade; the slide speed is what the mover actually did along the normal's plane. Knocks now take the switch state of the surface (`M.KNOCK_STATE`; plastic has no state, and sounds as wood).
- The real CET console does not reach these mods (`GetMod` is nil there; the port's own Lua host runs them, inferred from the mod folders): test by touching things and reading `[contact]`.

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
- Carried-body lag, the gun from the side (its turn is not swept) and what could leave Lua: analysed 2026-11-08 in `docs/hand-physics-lag-weaponstop-threading-20261108.md`. Built on branch `native-hand-physics` and played 2026-11-08 (not committed): state, what was tried and rejected, proved slots and how to read the logs are in `docs/native-hand-physics-build-20261108.md`. Read it before changing the carry. Open there: the game releasing a held actor while walking.

## How the user works

- Verify before deciding; probes go into the same build as the change they test.
- No code change without the user's approval; install only with the game closed and a verified backup
  (`E:\cyberpunk_vr_port\install_gun_turned.ps1` backs up and installs the DLL, `init.lua` and `weapon_custom_boxes.lua`).
- Nothing is committed or pushed without the user's go-ahead. Branch `hand-interaction`, remote `fork` (EffinexX/cyberpunk-vr-port).
- Build: `$env:CMAKE_GENERATOR_INSTANCE='E:/Programs/VSBuildTools'; cmake --build build --config Release --target cyberpunkvrport_stereo`
  (incremental ~0.2 min).
