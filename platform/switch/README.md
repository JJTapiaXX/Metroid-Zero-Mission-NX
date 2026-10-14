# Metroid: Zero Mission NX 1.2 — Switch instructions

## Installation and controls

Extract the installation ZIP to the SD card root and provide your own USA GBA ROM dump at `/switch/mzm/mzm.gba`. Launch `/switch/mzm/mzmnx.nro` through your homebrew environment.

- D-pad / left stick: movement. A: jump. B: fire.
- L / ZL: aim. R / ZR: missiles. Plus: game pause. Minus: game Select.
- R3: open/close the second-screen popup in R3 Popup layout. While the popup is open, gameplay pauses, L/R switch tabs, and the controller cursor can click with A. Touch also works.
- Side by Side, Flip Grip, Screen on Top, and Corner Beside: both screens remain visible; the second screen uses touch without pausing gameplay or displaying a controller cursor. Controller buttons stay assigned to gameplay.
- Flip Grip rotates both screens clockwise, with gameplay on the right and the second screen on the left. Indicators rotate too.

## Second screen

Tabs are Map, Status, States, Achievements, Options. The map uses the original pause-menu assets from your ROM, with zoom, area selection, and Follow. Active objective markers use the original yellow target sprite and the same hint/completion rules as the in-game pause map. Options contains General, Audio, and Display. Display provides separate R3 Popup, Side by Side, Flip Grip, Screen on Top, and Corner Beside buttons, scaling choices, and Show FPS.

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

The build creates `platform/switch/mzm-switch-bringup.nro`; install it as `mzmnx.nro`. Its title is `Metroid: Zero Mission NX`, author `JJTapia_XX`, version `1.2`. These metadata credits do not replace the upstream credits in the root README, `CREDITS.md`, and `THIRD-PARTY-LICENSES.md`.

Optional local regression tests also need `unicorn` and `Pillow`:

```powershell
python -m pip install unicorn Pillow
./platform/switch/test.ps1 -Python python
python platform/switch/tests/run_game_smoke.py platform/switch/build/native/mzm-switch-bringup.elf YOUR_ROM.gba --ui-only --ra-test --output test-output
```

The automated achievement tests simulate API responses. Hardware login, unlocks, and audio have been reported by the Switch tester, but no full playthrough or complete hardware compatibility matrix is claimed. Other ROM regions remain unverified for this adaptation.

## Wide scale

Display > Scale > Wide renders a centered 288x160 gameplay viewport: 24 additional room pixels on each side of the original 240x160 view. It reveals scenery rather than stretching the image. Small rooms keep balanced borders, sprites follow the centered scene, and the HUD/minimap remain anchored to the screen edges.

Lava, water, and acid extend into both Wide margins while stopping at room edges.

Room backdrop tints and palette gradients cover both Wide margins inside the room.

Beams and missiles remain visible through both centered Wide margins.

Room backgrounds are sampled from their original assets and stop at room boundaries. Item, area, and save prompts stay centered. Menus, cutscenes, and door/white transitions use the original viewport. Wide uses proportional fit in all five layouts and is saved with display preferences.

## Updating to 1.2

Replace `/switch/mzm/mzmnx.nro` with the new NRO. Keep your existing ROM, in-game saves, settings, achievement account file, and caches. Save states require their exact build and ROM, so states from earlier builds cannot be loaded in 1.2; use an ordinary in-game save to continue instead.

## In-app updater

Open Options > General > Check for updates. When a newer release file is available, choose Download and install, then close and relaunch the app after it reports success. The updater checks the latest release from JJTapiaXX/Metroid-Zero-Mission-NX and selects mzmnx.nro.

HTTPS certificates and the GitHub asset SHA256 are verified before installation. The downloaded NRO is checked for this app's identity and verified again after writing to SD. A .bak copy of the installed NRO is kept beside it; an installation error attempts to restore that copy. ROM, ordinary saves, settings, and achievement caches are not replaced. Save states still require their matching build.

Same-version silent updates are detected by file checksum. Older release assets are not offered over a newer development build. The updater is available starting with 1.1. Install 1.1 manually once to enable future in-app updates.

## Second-screen appearance

The second-screen interface uses navy panels, cyan borders, gold selection indicators, resource cards, and consistent framed controls. Save-state slot captions are centered for both Used and Empty labels. The original map grid, objective sprites, touch targets, and display layouts are retained.

Screen on Top uses the full display area for gameplay and places the smaller second screen over the top-right minimap. Corner Beside puts gameplay in the left three quarters of the display and the second screen beside it at the lower right. Both support Pixel Perfect, Fit, Stretch, and Wide, with live touch controls.

Screen on Top follows the scaled gameplay image at the top right; Corner Beside aligns with its bottom edge, including letterboxed scales. Corner Beside uses a compact 312x234 panel; its touch coordinates follow the panel position.

In Screen on Top, double-tap and hold the second tap to move the panel. Pinch with two fingers to resize it (256–640 pixels wide). The panel keeps its aspect ratio, stays within the display, and saves its position and size when all fingers lift. A short single tap is delivered after the 300 ms double-tap window. Gameplay continues during gestures.

Audio > SFX Boost cycles through 100%, 125%, 150%, 175%, and 200%, adjusting both sampled and PSG sound effects separately from music. The setting is saved and resets to 100% with audio defaults. Display > LCD cycles Off, Soft, and Grid; Off is the default. LCD affects gameplay only, including rotated layouts, with source-color processing and cached scaled rows rather than a full-screen postprocessing pass.

Corner Beside left-aligns gameplay in a 960-pixel-wide area without cropping. Its second-screen border aligns with the scaled gameplay image at the bottom in Pixel Perfect, Fit, Stretch, and Wide.

Display filters also include Scanlines: alternating rows are dimmed on gameplay only, without changing its colors. The lines rotate with gameplay in Flip Grip, and the setting is saved. Scanlines use cached scaled rows without a full-screen postprocessing pass.
