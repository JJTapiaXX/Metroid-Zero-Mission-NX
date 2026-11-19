# Metroid: Zero Mission NX — Switch 2 / Kaihen 1.4

First public Switch 2 / Kaihen release, version 1.4.

Native Switch 2 support uses **libounce and OunceVK** from [ouncebrew/ounce-devkit](https://github.com/ouncebrew/ounce-devkit). It retains the game's native C engine, the Switch second-screen UI, R3/touch controls, layout/filter/audio settings, Wide/Zoom Out, strict save states, and RetroAchievements integration.

## Latest fixes

- CPU composition runs in cached memory, then uploads the finished frame through Vulkan. Popup dimming, scaling, and filters no longer read potentially uncached GPU upload memory.
- Runtime diagnostics use a bounded, lower-priority SD writer. A busy card drops diagnostic records instead of stalling gameplay; the independent native crash file remains available.
- Achievement unlock journals and downloaded badge caches are queued to a background writer. Authenticated snapshots own their data; repeated journal updates coalesce, unlock journals take priority over optional badges, and normal shutdown flushes pending writes. Temporary-file/backup protection and offline cache authentication are preserved. Very recent queued writes still require time to reach the card, as with other asynchronous saves.
- A two-second black-frame Vulkan warm-up runs before the intro, servicing pending startup account/cache responses so first-use work can finish before the “Emergency Order” text starts. No gameplay or cutscene time is advanced during that warm-up.
- Save-write durations and full-frame interval/peak timings are recorded for troubleshooting remaining stalls. Audio-content logging has not been reintroduced.
- Includes the new connected atlas, collectible breakdown, and downloaded-map indicators described in [the shared Switch update notes](switch-unreleased.md).

## Credits

**JJTapia_XX** — Switch adaptations, feature direction, hardware testing, and packaging. **tinaut1986** — original 3DS port and second-screen UI reference for the area overview, collectible breakdown, percentages, and downloaded-map indicators. Source provenance and exact upstream references are in [CREDITS.md](../../CREDITS.md) and the shared notes. The connected multi-area atlas is a new Switch integration, rather than a feature falsely attributed to the upstream screenshot. **metroidret/mzm**, YohannDR, EstebanPdN, VirtuaPPU/Mathéo Vignaud, rcheevos/RetroAchievements, devkitPro, switchbrew/libnx, libounce, OunceVK/Mesa, and all retained upstream contributors are credited in the source/package notices. Original licenses remain in effect.

## Install and test

Put the Switch 2 executable and your own supported ROM at `switch/mzm-switch2/mzmnx-switch2.nro` and `switch/mzm-switch2/mzm.gba`. Normal saves/settings remain in that folder; older save states are rejected across builds. The in-app updater fetches only published `nx-switch2-v*` releases and `mzmnx-switch2.nro`. SHA-256 and Switch 2 executable identity checks prevent cross-console installation; backup/rollback checks remain enabled. Close and relaunch after installation. No ROM or extracted game assets are included.

Earlier builds have been launched and played on JJTapia_XX's Switch 2. The new UI, achievement-write changes, and startup warm-up still require the planned hardware test pass. Automated checks use emulated AArch64, real ROM map assets, and mock GPU/network/filesystem services; they do not establish hardware FPS or a completed Switch 2 playthrough.

## Audio default

New installations default to 100% master volume. Existing saved volume settings are preserved.
