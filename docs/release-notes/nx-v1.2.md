# Metroid: Zero Mission NX 1.2

- Added an in-app updater under Options > General with verified GitHub downloads, an NRO backup, and rollback on installation failure.
- Added the original yellow objective marker to the second-screen map, matching the pause menu's activation and completion rules and following map zoom.
- Polished the second screen with navy panels, cyan borders, gold selections, resource cards, and clearer controls.
- Centered the labels in all five save-state slots, for both Used and Empty slots.
- Includes the current centered Wide fixes for sprites, beams and missiles, liquid layers, room-edge backgrounds, tint effects, minimap placement, and confirmation text.
- Retains strict save-state build and ROM validation; the temporary test-only bypass is not included.

## Install or update

Extract `Metroid-Zero-Mission-NX-1.2.zip` to your SD card. The app belongs at `switch/mzm/mzmnx.nro`. Keep your own compatible ROM at `switch/mzm/mzm.gba`; no ROM is included.

Keep existing ordinary in-game saves, settings, and achievement caches. Save states from earlier builds require their original NRO and cannot be loaded in 1.2. Continue from an ordinary in-game save.

Install 1.2 manually once to gain the updater. Future updates are checked under Options > General. Close and relaunch after a successful installation.

## Testing and limitations

UI controls, objective markers, updater asset selection/verification/rollback, and strict state validation are covered by local regression tests. GitHub metadata and download redirects were checked live. The updater's HTTPS transfer and installation still need confirmation on physical Switch hardware. USA ROMs have been tested; EU/JP ROMs and a full playthrough have not been verified.

## Credits and source

Switch adaptation by JJTapia_XX, based on tinaut1986/mzm and metroidret/mzm. Contributor credits, license notices, and the AI-assisted development disclosure are preserved. Corresponding source and build instructions are included in the source asset and release tag.

## New second-screen UI

Screenshots captured on Nintendo Switch.

### Map and objective marker

![Map and objective marker](https://raw.githubusercontent.com/JJTapiaXX/Metroid-Zero-Mission-NX/release/nx-v1.2/docs/images/ui-map.jpg)

### Status

![Status](https://raw.githubusercontent.com/JJTapiaXX/Metroid-Zero-Mission-NX/release/nx-v1.2/docs/images/ui-status.jpg)

### Achievements

![Achievements](https://raw.githubusercontent.com/JJTapiaXX/Metroid-Zero-Mission-NX/release/nx-v1.2/docs/images/ui-achievements.jpg)

### Options and updater

![Options and updater](https://raw.githubusercontent.com/JJTapiaXX/Metroid-Zero-Mission-NX/release/nx-v1.2/docs/images/ui-options.jpg)

