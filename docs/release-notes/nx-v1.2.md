# Metroid: Zero Mission NX 1.2

- Added Screen on Top: a compact, live second-screen overlay. Double-tap and hold the second tap to drag it, or pinch with two fingers to resize. Its position and size are saved when your fingers lift.
- Added Corner Beside: gameplay on the left with the second screen at the lower right. Its visible bottom border aligns with gameplay in Pixel Perfect, Fit, Stretch, and Wide.
- Added optional LCD Soft, LCD Grid, and Scanlines gameplay filters. Filters default to Off, leave the second-screen UI unchanged, and rotate with gameplay in Flip Grip.
- Added saved SFX Boost from 100% to 200%, adjusting sampled and PSG effects separately from music. Default is 100%; audio reset restores it.
- Preserved R3 Popup, Side by Side, Flip Grip, the polished second-screen UI, map objectives, in-app updater, and current Wide rendering fixes.
- Retained strict save-state build and ROM checks, with compatible migration and backup recovery for older audio/display settings.

## Install or update

Extract `Metroid-Zero-Mission-NX-1.2.zip` to your SD card. The app belongs at `switch/mzm/mzmnx.nro`. Keep your own compatible ROM at `switch/mzm/mzm.gba`; no ROM is included.

Keep ordinary in-game saves, settings, achievement account files, and caches. Save states require their exact original build and ROM; continue from an ordinary in-game save when updating to 1.2.

Players with the 1.1 updater can use Options > General > Check for Updates after this release is published. Close and relaunch after installation. Earlier builds can be updated manually.

## Screen on Top demonstration

GIF captured on Nintendo Switch. Double-tap and hold to move; pinch to resize. A single short tap waits for the 300 ms double-tap window before activating its normal control. Gameplay continues during gestures.

<img src="https://raw.githubusercontent.com/JJTapiaXX/Metroid-Zero-Mission-NX/release/nx-v1.2/docs/images/screen-on-top.gif" alt="Moving and resizing Screen on Top" width="400">

## Corner Beside and Scanlines

Screenshot captured on Nintendo Switch.

<img src="https://raw.githubusercontent.com/JJTapiaXX/Metroid-Zero-Mission-NX/release/nx-v1.2/docs/images/corner-beside-scanlines.jpg" alt="Corner Beside and Scanlines" width="75%">

## Testing and limitations

Local tests cover gesture handling, touch mapping, saved placement, settings migration and backup recovery, all native/Wide scale combinations, sampled/PSG effects gain separation, filter rotation, unchanged UI colors, and exact restoration when filters are Off. The supplied GIF and screenshot demonstrate the layouts on Switch hardware.

Soft is lighter than Grid or Scanlines in compositor instruction measurements; these measurements are not Switch FPS guarantees. Off is the default. USA ROMs have been tested; EU/JP ROMs, a full playthrough, and a complete hardware compatibility matrix remain unverified. Full in-app updater installation still needs hardware confirmation. RetroAchievements supports Softcore only.

## Credits and source

Switch adaptation by JJTapia_XX, based on tinaut1986/mzm and metroidret/mzm. Original contributor credits, licenses, and the AI-assisted development disclosure are preserved. Corresponding source and build instructions accompany the binary.
