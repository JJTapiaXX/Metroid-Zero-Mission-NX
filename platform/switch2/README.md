# Metroid: Zero Mission NX — Switch 2 / Kaihen

Experimental native AArch64 port using [ounce-devkit](https://github.com/ouncebrew/ounce-devkit), libounce, and [OunceVK](https://github.com/ouncebrew/ouncevk). Requires a Switch 2 with a working Kaihen homebrew environment. This is a separate binary from the original Switch port.

## Install

Copy `mzmnx-switch2.nro` into `sdmc:/switch/mzm-switch2/`. Put your own supported, clean Zero Mission ROM in the same folder as `mzm.gba`. No ROM or extracted game assets are distributed.

Launch the NRO through your Kaihen homebrew launcher. Logs are written under `switch/mzm-switch2/logs/`: `runtime.log`, `crash-last.txt`, and `ouncevk.log`. The last file captures the Vulkan driver's stderr output.

The game retains R3 popup/touch input, Map/Status/States/Achievements/Options tabs, layout modes including both Flip Grip orientations, audio settings, filters, Wide and Zoom Out. These features reuse the existing engine and compositor; they have not yet been confirmed on Switch 2 hardware. RetroAchievements connectivity and system keyboard/audio services likewise need hardware verification.

Normal in-game saves can be copied from the original Switch folder. Save states remain specific to their exact executable build and ROM; use an in-game save when migrating. Settings and caches live separately in the Switch 2 folder.

Use Options > General > Check for Updates to install newer Switch 2 builds. The updater accepts only published, non-prerelease `nx-switch2-v*` releases and `mzmnx-switch2.nro`, verifies the GitHub SHA-256 digest and Switch 2 application title, and keeps a backup during installation. Switch 1 releases are excluded. Close and relaunch after installing.

## Build

Install devkitPro's devkitA64 and devkitARM compilers, switch-tools, Python, and `pyelftools`. Install ounce-devkit following its upstream instructions, then run from the project root:

```sh
python platform/switch2/build_game.py --devkitpro /opt/devkitpro --jobs 4
```

An SDK extracted into a separate directory is also supported:

```sh
python platform/switch2/build_game.py --devkitpro C:/devkitPro --ounce-sdk /path/to/sdk-root --jobs 4
```

The SDK root must contain `libounce/` and `portlibs/switch2/`. The builder reads CPU flags from `libounce/switch2_cpu.mk`, compiles against libounce with `__SWITCH2__`, and links OunceVK's `libvulkan.a` before libounce. It uses libounce's startup/linker files, not the original Switch runtime. Build output and objects are isolated under `platform/switch2/`.

The original target is still built with `python platform/switch/build_game.py`; its output is unchanged.

## Renderer

The Map tab also offers **WORLD >**, a connected seven-area atlas with zoom/pan/follow controls. Tap **FIT WORLD** to see the whole atlas or **< AREA** to return. The item badge and Status footer open a per-area collectible/downloaded-map table with a hide-totals toggle. The original 3DS UI reference is credited to **tinaut1986** in source comments, CREDITS.md, and the upcoming release notes. The multi-area atlas is a new schematic Switch integration; it does not reveal unvisited rooms.

The native game/PPU and existing screen-layout/filter compositor render into a cached 1280×720 RGBA CPU canvas. A single sequential copy uploads the completed frame to persistently mapped Vulkan staging memory. Scaling row reuse, popup dimming, filters, and indicators never read from GPU-mapped memory, which may be uncached even when it is host-coherent. Vulkan uploads the staging buffer into an image and uses a nearest-neighbor blit to the presentation extent. Screen composition still runs on the CPU; this is not a GPU rewrite of the emulated GBA PPU.

The backend selects a graphics/present queue, checks transfer format support, flushes noncoherent host memory when required, waits before reusing the staging buffer, and uses a separate presentation semaphore for each swapchain image. It recreates the swapchain on out-of-date/suboptimal presentation and logs GPU failures. A display failure requests clean game shutdown instead of repeatedly waiting on a failed submission.

## Testing status

JJTapia_XX confirmed that the first Switch 2 build launches and runs on hardware, but reported severe R3 popup slowdown and transient drops after a Long Beam pickup. This revision moves all CPU screen composition out of potentially uncached Vulkan mappings. JJTapia_XX subsequently reported that performance was running well after the rendering and save-stall fixes; the newest updater and startup/cache changes still need hardware confirmation. Low-rate `gfx` timing entries in `runtime.log` distinguish GPU waiting, CPU composition, upload, and submission; the existing engine timings also distinguish gameplay, PPU, and UI work. Popup-loop timings are now refreshed instead of retaining stale gameplay measurements.

The follow-up save-stall build also queues runtime diagnostics to a bounded, lower-priority background SD writer. The gameplay thread does not flush the runtime log; if the card cannot keep up, diagnostic records are dropped rather than blocking frames. Native crash reporting still uses its independent pre-opened file. `save write_us` entries report completed save writes, and `gfx frame_gap_us`/`peak_gap_us` include time outside the sampled rendering sections. Actual save-file writes retain the existing atomic temporary-file/backup behavior and background SRAM worker.

Achievement journals and badge-cache writes now use a separate background queue as well. Signed journal snapshots coalesce and take priority over optional badge images; pending work is flushed on normal shutdown. The intro is preceded by a two-second black-frame Vulkan warm-up. These latest changes still need hardware confirmation; see the included Switch 2 release notes for the test-release changelog.

Compilation/linking and the existing portable compositor/input/audio tests can be checked on a development computer. Hardware validation is still required for launch, GPU presentation, controller/touch input, audio, keyboard/networking, suspend/resume, dock transitions, and extended gameplay. No claim of a completed Switch 2 playthrough is made.

## Credits and licenses

Switch adaptations: JJTapia_XX. Based on tinaut1986/mzm and metroidret/mzm; the project's GPL license and upstream/third-party notices continue to apply. Ounce-devkit and its Vulkan example informed the platform integration; libounce, OunceVK/Mesa, and Kaihen retain their upstream licenses. See the source packages and `THIRD_PARTY_NOTICES.md` for SDK notices. No Nintendo SDK code or game assets are included.
