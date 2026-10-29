# Metroid: Zero Mission NX 1.3

## New: Zoom Out

**Zoom Out** reveals more of the world on all four sides with a **320x192** gameplay view. Wide remains available at 288x160, alongside the original 240x160 view. Choose **Options > Display > Scale > Zoom Out**. Existing layouts and filters support it; menus, cutscenes, and transitions retain their native presentation where needed.

### Fit / original view

<img src="https://raw.githubusercontent.com/JJTapiaXX/Metroid-Zero-Mission-NX/release/nx-v1.3/docs/images/v1.3/fit.jpg" alt="Fit / original view gameplay comparison" width="75%">

### Wide

<img src="https://raw.githubusercontent.com/JJTapiaXX/Metroid-Zero-Mission-NX/release/nx-v1.3/docs/images/v1.3/wide.jpg" alt="Wide gameplay comparison" width="75%">

### Zoom Out

<img src="https://raw.githubusercontent.com/JJTapiaXX/Metroid-Zero-Mission-NX/release/nx-v1.3/docs/images/v1.3/zoom-out.jpg" alt="Zoom Out gameplay comparison" width="75%">

## Fixes and improvements

- Corrected expanded-view sprite positioning and clipping: Samus at the top edge, enemies, projectiles, and effects. Inset the Zoom Out HUD/minimap and preserved the minimap crop to prevent exposed white border pixels.
- Extended clouds, fog, liquids, background tints, and sky presentation into the extra view. Fixed sky-top wrapping into unrelated bottom tiles.
- Added stationary flooring beneath save stations. It selects a flat panel from each room, avoids slopes, and stays still while the station animates.
- Optimized rotated FPS-counter drawing and sprite scanline caching. Both Flip Grip orientations offer Pixel Perfect, Wide, and Zoom Out; Fit and Stretch are omitted in those layouts.
- Fixed the crash after the young-Samus/Ruins Test cutscene by including missing native animation-layout conversions.
- Fixed crashes in the final ship explosion and post-credits results text, including the New Record banner. Added full escape and post-credits regression tests.
- Restored the Power Bomb expansion/retraction effect in normal, Wide, and all four Zoom Out margins.
- Added explicit RetroAchievements username/password prompts, with Next and Sign in buttons.
- Achievement checks now run during ending scenes and game-mode transitions as well as gameplay. Official achievement conditions still determine unlocks; missed events are not retroactively granted. The live boss-specific unlock remains to be confirmed.
- Retained strict save-state build, ROM, checksum, and truncation checks. Temporary development builds that bypassed the build check are not used for this release.

## Start-to-finish testing

**JJTapia_XX tested the whole game from start to finish on a physical Nintendo Switch during 1.3 development**, including the final boss, escape, credits, ending illustration, and post-credits scenes. Crashes found during that playthrough were investigated and fixed; the affected escape and results sequences also have targeted automated native-engine tests.

Automated checks cover expanded rendering and native-view preservation, all six layouts and filters, Power Bomb shapes, save-station floor variants, save-state rejection, UI/settings, and achievement event handling/offline sync with a simulated service. This does not claim every setting combination, ROM region, or live achievement condition was exhaustively tested. USA ROM testing is documented; EU/JP remain unverified.

<details>
<summary>Ending screenshot from hardware testing (spoiler)</summary>

<img src="https://raw.githubusercontent.com/JJTapiaXX/Metroid-Zero-Mission-NX/release/nx-v1.3/docs/images/v1.3/ending.jpg" alt="Ending illustration captured during Switch testing" width="75%">

</details>

## Install or update

Extract **Metroid-Zero-Mission-NX-1.3.zip** to your SD card. Install the executable at `switch/mzm/mzmnx.nro`. Keep your own USA ROM at `switch/mzm/mzm.gba`; no ROM is included.

Keep normal in-game saves, settings, and achievement account/cache files. **Older save states require their original executable build and will be rejected by 1.3. Continue from a normal in-game save and create new states.**

After publication, use **Options > General > Check for Updates**, then close and relaunch after installation. This unpublished draft is not offered by the updater.

## Credits and license

Switch adaptation and hardware testing: **JJTapia_XX**. Based on **tinaut1986/mzm** and the **metroidret/mzm** decompilation. Upstream credits, GPL licensing, and third-party notices remain in the source and download packages. No ROM or extracted game assets are bundled. AI assisted development; changes were checked through automated tests and hardware feedback.
