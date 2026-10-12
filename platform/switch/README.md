# Metroid: Zero Mission NX 0.1 — Switch instructions

## Installation and controls

Extract the installation ZIP to the SD card root and provide your own USA GBA ROM dump at `/switch/mzm/mzm.gba`. Launch `/switch/mzm/mzmnx.nro` through your homebrew environment.

- D-pad / left stick: movement. A: jump. B: fire.
- L / ZL: aim. R / ZR: missiles. Plus: game pause. Minus: game Select.
- R3: open/close the second-screen popup in R3 Popup layout. While the popup is open, gameplay pauses, L/R switch tabs, and the controller cursor can click with A. Touch also works.
- Side by Side and Flip Grip: both screens remain visible; the second screen uses touch without pausing gameplay or displaying a controller cursor. Controller buttons stay assigned to gameplay.
- Flip Grip rotates both screens clockwise, with gameplay on the right and the second screen on the left. Indicators rotate too.

## Second screen

Tabs are Map, Status, States, Achievements, Options. The map uses the original pause-menu assets from your ROM, with zoom, area selection, and Follow. Options contains General, Audio, and Display. Display provides separate R3 Popup, Side by Side, and Flip Grip buttons, scaling choices, and Show FPS.

Master volume is in Audio only: 0–150%, saved between launches, defaulting to 50% when no preference exists. Audio filters are optional. Audio measurements have been removed from runtime logs.

## Saves

Normal in-game SRAM saves and five save-state slots are separate. Save states require this exact executable build and ROM. Make new states after updating. A state restarts music and restores game data and supported achievement trigger progress; earned unlocks are retained independently. Corrupt, incomplete, different-build, and different-ROM states are rejected.

## RetroAchievements

Sign in in Achievements using the Switch keyboard. Passwords are sent over certificate-verified HTTPS and never saved. The server token is saved in `ra-account.cfg`; keep that file private. This version supports Softcore only, with cached definitions, durable pending unlocks, and server confirmation. It does not implement Hardcore, leaderboards, or subsets.

Connect once to load your set and badges. Subsequent play can use cached definitions and images offline. Missing badges download in the background to `ra/badges/`. The list and popup share these images. “0 pending” means no unlocks are waiting to sync. The first entry (the Hardcore warning) is hidden from the player list and displayed totals.

## Troubleshooting

Logs live in `/switch/mzm/logs/`: `runtime.log`, `runtime-previous.log`, `crash-last.txt`, and `retroachievements-network.log`. Some operating-system force closes bypass the exception handler. Copy logs before another launch if reporting a crash, and review them before sharing. Never upload your ROM, token file, saves, states, or account cache.

## Build from source

Install devkitPro with devkitA64, devkitARM, libnx, and Switch tools, plus Python 3 with `pyelftools`. The checked-in generated ROM-layout metadata is included; no ROM is shipped with the project.

Windows (tested build host):

```powershell
python -m pip install pyelftools
python platform/switch/build_game.py --devkitpro C:/devkitPro
```

The build creates `platform/switch/mzm-switch-bringup.nro`; install it as `mzmnx.nro`. Its title is `Metroid: Zero Mission NX`, author `JJTapia_XX`, version `0.1`. These metadata credits do not replace the upstream credits in the root README, `CREDITS.md`, and `THIRD-PARTY-LICENSES.md`.

Optional local regression tests also need `unicorn` and `Pillow`:

```powershell
python -m pip install unicorn Pillow
./platform/switch/test.ps1 -Python python
python platform/switch/tests/run_game_smoke.py platform/switch/build/native/mzm-switch-bringup.elf YOUR_ROM.gba --ui-only --ra-test --output test-output
```

The automated achievement tests simulate API responses. Hardware login, unlocks, and audio have been reported by the Switch tester, but no full playthrough or complete hardware compatibility matrix is claimed. Other ROM regions remain unverified for this adaptation.

## Wide scale (fix14)

Display > Scale > Wide renders a 288x160 gameplay viewport, revealing 48 additional room pixels on the right of the original 240x160 view. Tiles are read from the room map rather than stretched or repeated. The camera and room logic retain the original game behavior. Menus, cutscenes (including in-game cinematics), and room-loading transitions fall back to the original viewport. Wide uses proportional fit in all three layouts and is saved with display preferences.

Fix15 preserves retail sprite visibility in Wide: the additional columns show scenery, while sprites keep their original bounds and off-screen parking behavior. Door and white room fades temporarily use the original viewport until the transition completes, preventing unfinished graphics from being revealed.

In Wide, the minimap is anchored to the expanded right edge. Identity-tagged item/area message banners and the save yes/no cursor are centered together; ordinary sprites keep the original bounds.

Fix16 corrects the minimap as two tagged HUD sprites rather than moving BG0 room graphics. Only reconstructed room-map layers may render in the added columns; fixed-screen layers keep their native bounds. Out-of-room cells are explicitly transparent, rather than incorrectly drawing character tile zero. Norfair room data is covered by the regression suite.

Fix17 keeps valid minimap and dialog sprite pixels visible over blank space beyond room edges, while continuing to suppress invalid background tiles. Regression cases cover direct, windowed and blended rendering.

Fix18: Wide renders identity-tagged world sprites in the additional room columns using their unwrapped world position. Chozo statues and enemies become visible as they enter the expanded view; untagged parked OAM remains hidden. Cutscene and door-transition viewport guards are retained.
