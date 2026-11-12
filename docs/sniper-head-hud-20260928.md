# Sniper nest head following and HUD

Verified in the running q303 sniper sequence on 28 September 2026, PID 26464.
The controlled device was `SniperNest`, EntityID 14623291074227584262, with
`DeviceCameraControl6858` and `cameraComponent`. The attached weapon was
`Items.Sniper_Nest_Version_Tech_Sniper_Rifle`.

## Motor

The native controller uses the same input/update path as SurveillanceCamera,
but the port's explicit class check rejected SniperNest. Extend that check to
these two exact classes. Existing owner, lens, tracking, input-capture and
freshness guards remain. The original native controller still applies its
limits and animation; its automatic modifiers are filtered through the existing
same-update receipt, avoiding feedback from the VR view into the motor.

Sniper acquisition also aligns the physical barrel to the rendered base plus
HMD. Its original elevation and nonzero initial head yaw cannot remain as an
aim offset. Subsequent updates use the established head-delta model. This
initial alignment is sniper-specific; surveillance acquisition is unchanged.

In the old running build, the initial lens was pitched up about 73 degrees.
After enabling the controller and correcting the acquisition offset, measured
lens/view disagreement was 0.034 degrees. The user confirmed head following.
No shots were fired by the agent.

## HUD

The active `hudSniperNestController` is nested inside the existing
`briefing_sequence_player` entry, not a distinct sniper HUD entry. Its source
window and outer Root are 3840x2160 and already use the native separate-window
path. Keep that live controller and texture.

While the validated sniper motor publishes the current takeover identity,
route only `briefing_sequence_player` into the Surveillance HUD channel. This
channel follows the complete HMD pose immediately, with zero freelook cone,
independent of the normal HUD's follow setting. A changed/expired takeover
restores ordinary briefing behavior. Update existing slot masks' channel
atomically when the entry changes groups.

The Lua bounds bridge recognizes the nested sniper controller and uses its
existing outer briefing Root as the capture root. No widget is reparented or
duplicated, and the source layout retains its authored dimensions.

## Verification And Deployment

- 36 HUD tests passed, including a sniper-channel GPU test for both eyes,
  pitch/yaw/roll, sub-cone movement, independent textures and zero follow lag.
- Both surveillance tests passed, including native input ABI, frame rates,
  camera switching, tracking discontinuities, new sniper acquisition alignment
  and deferred alignment while a menu captures input.
- A lexical CET fixture passed for nested sniper bounds, resize, visibility and
  unrelated controllers. It did not access the live Game object.
- The session helper's filter ABI test passed before its two checked MinHook
  patches were installed. The original motor and HUD capture paths then ran for
  SniperNest; the user confirmed both changes worked visually.
- Release compilation succeeded. At the user's explicit request, the game was
  terminated and DLL, matching PDB and hud_panel.lua deployed. It was left closed.
  The temporary session helpers are not part of the installed mod.
- UserSettings.json and vrik_calibration.ini hashes remained unchanged.

Installed DLL SHA-256:
`BE8647B40314445A1572E20E3D6B388B758D5567F7EFCA973A3AD6AFBB201651`.
Installed Lua SHA-256:
`FEFE9C97E3843BB162FD6C2FC6F2FE4119D881087AB604C2E895EEB293EB7AE0`.
Backup and verified deployment receipt:
`build/sniper-head-hud-20260928/deploy-185012/manifest.json`.

## Input in the nest: LT zoom, RT fire and the LB scanner (2026-11-12)

Reported in q303 "You Know My Name": inside the SniperNest neither the zoom (LT), the shot (RT) nor the scanner
(LB) reached the game. A device takeover runs the gameplay branch of `Hooks/XInput.cpp`, not the menu/braindance
pad branch (`g_bdActive` is raised only by the braindance/scene camera natives, not by `g_remoteCamOn`), and V
holds no weapon of her own in the nest, so every "empty hands" rule applied:

- **LT:** the smoking lighter's gate (`ltClaimedByLighter`) zeroed LT with no weapon on foot. Its old comment
  named an `xr_lt_needs_weapon` ini key that does not exist; the only switch is the export
  `CyberpunkVR_LtLighterGate`.
- **RT:** "empty-hand RT is a finger gesture" (`g_VRBind==4 && !g_hasWeaponEquipped`) zeroed RT; a selected arm
  cyberware (`physicalCyberware`) and the launcher's LT->RT swap would have done the same.
- Fix (both): these rules are skipped while `DeviceCamActive()` (`deviceTakeover`). User confirmed LT and LB
  arrive after the first build (21:51); RT fix built second.
- **LB:** no port-side block. The user's SteamVR binding (Index, workshop 3838606856, made 2026-11-12) puts the
  left trackpad centre, sub_mode click, on `frame_bumper`, which `OpenXRFrameLoop.cpp` emits as LB.

### Scanner toggle on LB

The user: "since 0.2.3 the scanner is no longer a toggle". Checked against git: the 0.2.3 merge (`206dfc87`)
only tightened the ear gesture's squeeze (`ScannerSqueeze`); its latch (`CyberpunkVR_ScannerToggle`) is unchanged.
The difference is the input: the previous binding (workshop 3836115085) had nothing on the left trackpad, so the
scanner was the ear gesture (a toggle); the new LB button went through raw, and the game's LB is
Vision_Hold_Button, a hold. Per the user's request, LB now toggles too (`CyberpunkVR_ScannerButtonToggle`, default
1): the raw LB is masked out of the merge on gameplay screens (not in a vehicle, menu, popup or the lobby) and
asserted from its own latch. On foot `ScannerRequest` releases a press the game did not take within 750 ms; in a
takeover that check is skipped, because it is not verified that PlayerStateMachine.Vision reports the device's
scanner, and the latch is cleared when the takeover starts or ends. While the scanner is open RT tags instead of
firing (`CyberpunkVR_ScannerTriggerTag`), in the nest as on foot.

Installed 2026-11-12 22:03, DLL SHA-256 `CA67C7246DAAD026963BF90A185013817372819F12DFDE5A64B4BD765A868D34`,
backup `E:\cyberpunk_vr_portackupst-scanner-toggle-20261112-220359` (the LT-only build of 21:51,
`9C863E76...`, backup `lt-takeover-20261112-215138`). In-game confirmation of RT and the toggle pending.
