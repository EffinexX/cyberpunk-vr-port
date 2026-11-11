# Merging upstream 0.2.3 (2026-11-11)

Merge commit `206dfc87` on `native-hand-physics` (upstream `e9139450`, tag `0.2.3`). Installed 2026-11-11 20:04, **not yet
played**. Backup of every replaced game file: `E:\cyberpunk_vr_port\backups\pre-merge-023-20261111-200453`.

## What to know for the next upstream merge

- **Upstream force-pushes `main` and squashes each release into one commit** on the previous release. Our old copy of
  `main` (`87d97327`, a README commit) is not in its history any more. The merge base was the 0.2.2 commit `0a2f4f10`.
- **Shared slots collide silently.** Upstream took slot 174 for `kReloadGlowOff`, the slot of our flashlight gesture.
  Git showed it as a text conflict only because both lines sat together. The flashlight moved to 175 (plugin, the CET
  mod in `mods/cet-standalone`, its notes). After a merge, check `include/Utils/SharedSlots.hpp` for doubled numbers, and
  literal slot numbers in Lua (`SharedSlot(200..202)` are used by Optics and the debug draw).
- **The packed archive is no longer tracked** (upstream: over GitHub's size limit; `.gitignore`). A fast-forward to the
  merge **deleted `mods/archive/cyberpunkvrport.archive` from the working folder**; it was taken back out of `206dfc87^1`.
- **The archive holds more than `mods/archive/source`** (generated meshes etc.), so it was not rebuilt from source. Done
  instead: WolvenKit `unbundle` of upstream 0.2.2 (from git), ours, and 0.2.3 (from the release zip
  `CyberpunkVRPort-0.2.3.zip`, 150.9 MB). Ours differed from 0.2.2 in exactly 5 files, the player entities
  (`player_ma_fpp`, `player_wa_fpp`, `player_wa_fpp_replacer`, `player_ma_fpp_ep1`, `player_wa_fpp_ep1`), none touched by
  0.2.3 (92 files changed there). So 0.2.3's tree plus our 5, packed with `tools\wk.sh pack <dir named cyberpunkvrport>`;
  unbundled again and compared: identical, 1789 entries, 106,725,376 bytes. Work files in `E:\cyberpunk_vr_port\upstream-0.2.3`.
- **`scripts/deploy_full.ps1` would fail mid-install**: it reads `$hudLayout`, which is never set, under
  `Set-StrictMode -Version Latest`, after it has made backups and parked loaders. Not used; installed by hand from the
  `build_dist.ps1` package (`-BuildDir build`; its default `build\patchday\sdk25-build` does not exist here), only files
  whose hash differed (58, 7 new), keeping the installed `vrcam.json` resolution as `deploy_full` does.
- Building a fresh tree (a worktree) needs the submodules copied in and the FidelityFX SDK under
  `build\fidelityfx-native` (it is downloaded otherwise).

## Conflicts and how they were settled

| File | Settled |
|---|---|
| `include/Utils/SharedSlots.hpp` | Both; flashlight to 175 |
| `src/Hooks/RoomscaleMove.cpp` | Our `!HeldInWorkspot()` with upstream's `VRIK_EffectiveCutsceneSuspendTier()` |
| `README.md` | Upstream's: it dropped the Valentino Holsters paragraph, the holsters are built in now |
| `mods/archive/cyberpunkvrport.archive` | Untracked, as upstream |

40 more files were changed on both sides and merged by git without conflict. Upstream's new scanner squeeze
(`ScannerSqueeze`, press/release thresholds) and our `EarTriggerGesture` share only the ear zone; both build.
