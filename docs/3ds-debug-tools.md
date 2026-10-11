# 3DS port: on-device debugging tools

Reference for the on-device diagnostic tools in the 3DS port, added while
investigating [#17](https://github.com/tinaut1986/mzm/issues/17) (Samus's
death animation showing the wrong sprite/palette). Kept here so a future
session (or a future bug) doesn't have to rediscover how these work or how
to parse their output from scratch.

All of them live behind one button: bottom screen -> **DEBUG** tab ->
**HERRAMIENTAS DE DEPURACION** / **DEBUG TOOLS**, which opens a list where
each row triggers one tool. A second screen behind the **TELETRANSPORTE** /
**WARP** row holds the room-jump tools.

These were originally L+R+`<button>` chords held during gameplay. Those are
gone: the chord stole L/R/X/Y/START/SELECT from whatever the player had
mapped them to for the frame it fired, was easy to trip by accident, and had
to be memorized. The menu has none of those problems. Nothing else about
what each tool does changed, so the old section names below still name the
combo they used to be reached by, in parentheses.

**None of this exists in a plain build.** The button, the menu and every
action behind it are compiled out entirely -- not just hidden -- unless the
build defines `PORT_DEBUG_TOOLS_ACTIVE` (`source/port_debug_tools.h`).

There is exactly **one** flag: `make clean && make DEBUG_TOOLS=1` (defines
`PORT_DEBUG_TOOLS`, from which `port_debug_tools.h` derives
`PORT_DEBUG_TOOLS_ACTIVE`). It compiles in the menu *and* every throttled
per-frame diagnostic log stream (GPU, audio, PERF). The old per-system
opt-in flags (`PORT_GPU_RENDERER_DIAG_LOG`, `PORT_AUDIO_DIAG_LOG`, and an
ad-hoc `PORT_PPU_PERF_LOG`), each its own `EXTRA_CFLAGS`, are gone: a
`DEBUG_TOOLS=1` build carries all of them and you pick which one(s) actually
write from the **LOG** cell in the menu (see **Log to SD / buffering**
below). Decompiled `src/` files that can't include `port_debug_tools.h`
check `defined(PORT_DEBUG_TOOLS)` directly.

A plain production build (no flags) shows no button on the DEBUG tab and has
no source-level path to trigger any of these.

### File rotation

Every capture tool keeps the last N captures instead of overwriting one
fixed filename, and reuses the oldest slot once N exist
(`port/port_debug_files.c`):

| Capture | Names | Kept |
| --- | --- | --- |
| Frame-time recorder | `mzm-perf-NN.bin` | 10 |
| Screen dump | `mzm-dump-NN-left.rgb`, `mzm-dump-NN-vram.bin`, ... | 10 sets |
| Scene recorder | `mzm-rec-NN.bin` (+ `mzm-rec-NN-shot-MMMM.rgb`) | 4 |
| Debug log | `mzm-debug-NN.log`, one per logging session | 10 |

**The slot number is not a chronological order.** The newest capture is
whichever slot was free or least recently written, so order a fetched set by
modification time (an FTP listing shows it), never by name. Rotating the
names logrotate-style so `01` is always newest was rejected: it means
rewriting up to N files per capture, which for the 32MB scene recordings
costs more than it's worth.

Every file of one screen dump shares its set number, so a fetched set is
unambiguously one press. That matters beyond convenience: before this, a
set could silently end up a mix of two presses, which happened during the
issue #17 investigation and briefly looked like corrupted source data.

All dumped files land in `debug/` inside the game folder (`sdmc:/3ds/Metroid Zero Mission 3DS/debug/`), fetchable over FTP if the
console is running an FTP server (see `make ftp` / `FTP_HOST`/`FTP_PORT` in
`platform/3ds/README.md`).

## Screen dump (was L+R+X)

Menu row **VOLCADO PANTALLA** / **SCREEN DUMP**.

Implemented in `PlatformGpu3DS_DumpScreens` (`platform_gpu_3ds.c`). Writes,
all sharing one rotating set number, `NN` below (see **File rotation**):

| File | Contents |
| --- | --- |
| `mzm-dump-NN-left.rgb`, `-right.rgb`, `-bottom.rgb` | Headerless raw RGB8, top-to-bottom, **portrait** (unrotated) dimensions -- top screen targets are 240x400, bottom is 240x320. The real GPU-rendered output straight from VRAM (via `C3D_SyncDisplayTransfer`), not a CPU-side source buffer -- this is what's actually on screen, including any renderer bugs. |
| `mzm-dump-NN-vram.bin` | Full emulated GBA VRAM, `gVram[0x18000]`. OBJ tile data starts at byte offset `0x10000` (tile N is at `0x10000 + N*32` for 4bpp, `N*64` for 8bpp). |
| `mzm-dump-NN-io.bin` | Full emulated GBA IO register block, `gIoMem[0x400]`. Standard GBA IO map (GBATEK) applies directly -- e.g. `DISPCNT` at offset 0, `WIN1H`/`WIN1V` at `0x42`/`0x46`, `BLDCNT`/`BLDALPHA`/`BLDY` at `0x50`/`0x52`/`0x54`. |
| `mzm-dump-NN-bgpltt.bin`, `-objpltt.bin` | BG and OBJ palette RAM, 256 entries x u16 (BGR555) each, i.e. 16 banks of 16 colors. |
| `mzm-dump-NN-oam.bin` | OAM, 128 entries x 3x u16 (the 4th u16 per 8-byte slot is padding on real hardware and unused here). Standard GBA OAM attribute layout. |
| `mzm-dump-NN-samus.txt` | Plain text: `pose`, `currentAnimationFrame`, `walljumpTimer`, `suitType`, `suitMiscActivation`, the four per-body-part gfx DMA sizes (`shoulderGfxSize` etc.), `armCannonGfxUpperSize/LowerSize`, `unk_22`. See `PortPpuMzm_DumpSamusState` in `port_ppu_mzm.c`. |
| `mzm-dump-NN-samusdata.bin`, `-samusphysics.bin` | Raw `struct SamusData` / `struct SamusPhysics` (see `include/structs/samus.h`) for anything not already in the `.txt`. |
| `mzm-dump-NN-sprites.txt` | Plain text. First line: BG1 camera position, WIDE state (`wideActive`, `frameDrawn`, culling margins in sub-pixels), adaptive frame skip and New3DS profile. Then one `sprNN` line per existing `gSpriteData` slot (id, status bits, properties, position, draw distances, draw order, pose) and one `tagNNN` line per OAM slot carrying a WIDE origin tag (the true screen position the renderer uses to undo OAM's Y/X wrap). Tells "the game never put the sprite in OAM" apart from "it is in OAM but drawn elsewhere". |

**Why the split between `platform_gpu_3ds.c` and `port_ppu_mzm.c`:**
`platform_gpu_3ds.c` includes `<3ds.h>`/`<citro2d.h>`, whose `u32` typedef
(`unsigned long`) conflicts with the GBA-port one dragged in transitively by
`structs/samus.h` (`unsigned int`) -- a hard compile error if both end up in
the same translation unit. `port_ppu_mzm.c` already safely includes
`structs/samus.h` (and not `<3ds.h>`), so any code that needs real Samus
struct fields lives there and is called from `platform_gpu_3ds.c` through a
narrow extern (`PortPpuMzm_DumpSamusState`, `PortPpuMzm_GetSamusRecordState`)
instead of via a shared header. Keep new debug-dump code following this
split rather than trying to include both headers in one file.

### Reconstructing the screenshot

The `.rgb` files are portrait and need rotating 90° to view correctly
(confirmed empirically: `ROTATE_90` puts the on-screen "FPS 60" HUD text
upright and readable; `ROTATE_270` mirrors it).

**They are BGR, not RGB.** This page said `RGB` until 2026-09-04, so every
screenshot anyone reconstructed from a dump before then had **red and blue
swapped**. It survived unnoticed because the swap leaves greys, greens and
purples alone (purple is R+B) and only trades red for blue, which is easy to
accept as "the palette in that room". It was caught by the renderer
comparison harness, and settled from the data rather than by argument:
interpreting a dump's bytes as BGR matches colours in its own palette files
for 79% of pixels against 45% as RGB, and the residue in both cases is
blend/brightness output that is not a raw palette entry.

```python
from PIL import Image
with open('mzm-dump-01-left.rgb', 'rb') as f:
    data = f.read()
img = Image.frombytes('RGB', (240, 400), data).transpose(Image.ROTATE_90)
b, g, r = img.split()
img = Image.merge('RGB', (r, g, b))   # the dump is BGR
img.save('left.png')
# bottom target is 240x320 instead of 240x400
```

## Kill Samus (was L+R+SELECT)

Implemented in `PortPpuMzm_DebugKillSamus` (`port_ppu_mzm.c`), triggered from the
debug tools menu (`port_bottom_ui_3ds.c`). Zeroes
`gEquipment.currentEnergy` and calls `SamusSetPose(SPOSE_HURT_REQUEST)` --
the same real entry point lethal damage uses in
`SpriteUtilTakeDamageFromSprite` (`src/sprite_util.c`) -- so Samus starts the
real death sequence (`SPOSE_DYING`) on the next update, exactly as if she'd
been killed by an enemy, without needing to actually get hit down to 0
energy in gameplay first. No-op while already in a hurt/knockback/dying pose
so mashing the row mid-animation doesn't re-trigger it. Added while
iterating on issue #17 to make repeated death-sequence captures (screen dump,
scene recorder) fast to set up.

## Log to SD / buffering (LOG A SD, LOG EN BUFFER)

Two cells in the tools menu, backed by `port/port_debug_log.c`.

- **LOG A SD** -- cycles the log **mode**: `OFF` -> `ALL` -> `GPU` ->
  `AUDIO` -> `PERF` -> `OFF`. Starts at `OFF`, *even in a debug build*.
  While `OFF`, `Port_DebugLog` / `Port_DebugLogBuffered` /
  `Port_DebugLog_Gpu` / `_Audio` / `_Perf` all return immediately and
  nothing is written to the session's `mzm-debug-NN.log`: no SD I/O, no file
  growing unasked, no frame-rate cost from the per-frame diagnostic paths
  that are now always compiled in. `ALL` writes every stream; a single
  stream name writes only that one plus the always-on one-off checkpoints
  (`Port_DebugLog`) -- so a session chasing e.g. the GPU renderer's
  `GPUDIAG`/`PBFLASH` lines isn't drowned by audio or PERF output. Any
  transition flushes what's buffered so the tail of a capture isn't lost,
  and leaving `OFF` drops a `USER MARK: SD logging enabled` line so the
  start of each capture is easy to find in an appended log.
- **LOG EN BUFFER** -- ON by default. Lines accumulate in a 4KB RAM buffer
  and hit the card when it fills (one `fopen`/`fwrite`/`fclose` for the
  whole buffer). Turn it OFF to make *both* entry points write immediately,
  at ~18-20ms per call -- only worth it when chasing a hang that would
  otherwise swallow whatever is still in RAM.

Each time logging leaves `OFF` it claims a fresh `mzm-debug-NN.log`, so a
capture is self-contained and there is nothing to delete beforehand.
Switching between active modes (`ALL` -> `GPU` -> ...) keeps writing to the
same file, since it is still the same sitting. This replaced a single log
that was only ever appended to, never truncated -- where the documented
workflow was "delete it over FTP first, and hope you remembered".

## Log marker (was L+R+Y)

Drops a `"USER MARK: ..."` line with a timestamp into
the current session log (via `Port_DebugLog`) so a play session can flag
"something happened right here" without describing timing after the fact.
Grep the log for `USER MARK` and read the surrounding lines (set the **LOG**
mode to `GPU` or `ALL` for the `GPU_REJECT`/`GPUDIAG` lines) to see what the
renderer was doing at that moment.

## Depth-tint view (PROFUNDIDAD / DEPTH TINT)

A toggle in the tools grid. When on, `port_gpu_renderer.c` adds a **separate
pass after the normal render** that re-draws every BG layer and sprite in its
resolved stereo-tier colour, blended ~78% over the real image (a full-screen
backdrop layer would otherwise hide the scene) and keeping the source alpha so
silhouettes stay (`sDepthTint` / `Port_GpuRenderer_SetDepthTint`). It is a
separate pass, not interleaved with the main loop's texenv state machine --
the first cut interleaved it and produced per-eye flicker. One colour per
`PORT_TIER_*`, the same palette the layer workbench uses:

| Tier | Colour |
| --- | --- |
| `BG_FAR` | blue `#5b8fd6` |
| `BG_MID` | green `#6fae52` |
| `BG_PLAY` | amber `#e9a13b` |
| `BG_OVERLAY` | red `#d24f4f` |
| `OBJ_P1` | white `#f2f2f2` |
| `OBJ_HUD` | purple `#b06fd6` |
| `OBJ_MAP` | cyan `#4fc7c7` |

Exists to check, on the console, whether a cutscene's layers landed on the
planes `port_cutscene_depth.inc` assigned them -- a fast scene doesn't give
the eye time to judge the parallax, but a mislabelled layer's colour is
obvious. It only recolours; it does **not** change draw order, so a
parallax/occlusion contradiction still reads as the wrong layer being on top.
The border HUD pass is left untinted. No cost when off.

**CPU-rendered frames get a magenta border instead of tint.** Cutscenes that
use an affine BG mode (the Tourian-escape montage, e.g.) fail
`Port_GpuRenderer_CanRenderFrame`'s `mode != 0` check and fall back to the CPU
scanline renderer (`port/ppu/src/mode1.c`), which has **no stereo depth at
all** -- so `port_cutscene_depth.inc` overrides do nothing there and there is
no tier to colour. `port_ppu_mzm.c` stamps a 3px magenta ring on those frames
while the view is on, so a stretch of the cutscene with no 3D to tune is
obvious rather than looking like the tint silently stopped working.

## Scene recorder (was L+R+START)

Implemented in `PlatformGpu3DS_ToggleRecording` / `PlatformGpu3DS_RecordTick`
(`platform_gpu_3ds.c`), ticked once per emulated GBA frame from
`Port_Bios_Halt` (`port/port_bios.c`). Exists because a single screen dump
only ever catches one frame -- not enough to find the exact moment a
fast-changing scene breaks (e.g. issue #17's death animation, where the
first few frames -- a green palette flash -- turned out to be *correct*,
and the actual corruption happens some number of frames later).

- **First touch**: opens `debug/mzm-rec-NN.bin` (truncating any previous
  recording) and starts sampling.
- **While active**: every 4 frames (~15 samples/sec at 60 FPS,
  `kRecordEveryNFrames` in `platform_gpu_3ds.c`), appends one fixed-size
  record to the file: VRAM + IO + BG palette + OBJ palette + OAM (the same
  data as the screen dump, minus the RGB screenshots -- see below for why)
  plus a small header. Capped at `kRecordMaxSamples` (450, ~30s) so
  forgetting to stop it doesn't fill the SD card; recording
  auto-stops at the cap.
- **Touching the row again**: stops sampling and closes the file.
- A small red "● REC" indicator blinks on the bottom screen while active
  (`Port_BottomUI_Render` in `port_bottom_ui_3ds.c`), independent of which
  tab (map/status/debug/options) is currently open.

**Why no screenshots in every sample:** each RGB screenshot is ~800KB
combined (left+right+bottom) and needs a GPU display-transfer sync
(`C3D_SyncDisplayTransfer`). At 15 samples/sec that's either a frame-pump
stall or an unreasonable amount of buffering. The VRAM/OAM/palette state
alone is enough to reconstruct the visual frame-by-frame offline -- already
proven doing exactly that by hand for the screen dumps during the #17
investigation (see the Python snippet above, extended to decode OAM entries
and tiles; ask for the exact script if starting a fresh session on this).

**Companion screenshots, every `kRecordScreenshotEverySamples`-th sample
(default 4, i.e. ~4/sec):** a real left-eye screenshot (same mechanism as
`PlatformGpu3DS_DumpScreens`' single shot) is written to its own file,
`debug/mzm-rec-NN-shot-MMMM.rgb` (headerless raw RGB8, 240x400 portrait,
same rotate-90 handling as the screen dump `.rgb` files), where `NNNN` is the
0-indexed sample number -- i.e. it lines up with the Nth record when
splitting `mzm-rec.bin` per the snippet below. This is the only way to tell
apart "the emulated state was already wrong at this sample" (visible in the
VRAM/OAM/palette reconstruction alone) from "the state was correct but the
GPU renderer drew it wrong" (only provable by diffing the reconstruction
against what was actually on screen at that same sample) -- needed after a
2026-08-25 #17 session where a *different*, unsynced capture (a screen dump
whose files turned out to be a truncated/mismatched mix from separate
button presses) briefly looked like it showed corrupted source data, when
the actual recorder-based reconstruction across the whole death sequence
(done earlier in the investigation) showed every sample's VRAM/OAM/palette
reconstructing to a correct Samus silhouette while the screen itself showed
the scrambled sprite -- i.e. the bug is in the GPU renderer, not the game's
graphics loading. The per-sample screenshots exist to keep confirming that
without relying on eyeballing real hardware during a 3-4 second death
animation.

### `mzm-rec.bin` format

A sequence of fixed-size records, one per sample, back to back (no
end-of-file marker -- just read records until EOF). Each record is:

```
struct RecordHeader {   // 64 bytes total (was 32 with magic 'MZMR'; bumped to
                        // 'MZM2' for issue #20's perf instrumentation, then
                        // 'MZM3'..'MZM6' for the clip/camera block
                        // that follows VRAM -- see the clip block below)
    uint32_t magic;                 // 'MZM6' = 0x364D5A4D (read as little-endian bytes)
    uint32_t frameCounter;          // running emulated-frame counter at capture time
    uint32_t pose;                  // gSamusData.pose (SamusPose enum, see constants/samus.h)
    uint32_t currentAnimationFrame; // gSamusData.currentAnimationFrame
    uint32_t walljumpTimer;         // gSamusData.walljumpTimer
    uint32_t suitType;              // gEquipment.suitType (SuitType enum)
    uint32_t suitMiscActivation;    // gEquipment.suitMiscActivation (SuitMiscFlags bitmask)
    uint32_t unk_22;                // gSamusPhysics.unk_22 (arm cannon OAM draw-order flags)
    // -- perf extension (issue #20) --
    uint32_t lastFrameUs;           // wall-clock duration of the emulated frame that
                                    // just ended, in microseconds (~16675 = on budget)
    uint32_t drawingTimeX100;       // citro3d C3D_GetDrawingTime(), hundredths of ms
    uint32_t processingTimeX100;    // citro3d C3D_GetProcessingTime(), hundredths of ms
    uint32_t spriteCount;           // non-disabled OAM entries this frame (of 128).
                                    // MZM parks unused slots offscreen at Y=0xFF
                                    // instead of setting the disable flag, so this
                                    // is 128 in practically every frame -- use
                                    // visibleSpriteCount in word 15 instead.
    uint32_t affineSpriteCount;     // of those, how many use rotation/scaling matrices
    // -- draw-call census (issue #20, 2026-09-04). These three words were
    // 'reserved' (zero) in earlier recordings; the magic did NOT change,
    // since nothing moved and no existing field changed meaning. A recording
    // with all three zero is either an old one or a CPU-renderer frame.
    uint32_t drawCount;             // C2D_DrawImage calls, summed over both eyes
    uint32_t blendTransitions;      // opaque<->alpha switches; each one is a
                                    // C2D_Flush, i.e. a broken draw batch
    uint32_t packed;                // bits 0-7   visible sprites (intersecting 240x160)
                                    // bits 8-9   scissor passes: 2 while a GBA window
                                    //            splits the draw order, else 1
                                    // bit  10    window active
                                    // bit  11    haze active (offscreen BG3 RTT pass)
                                    // bits 16-31 haze tile count
};
// followed immediately by, back to back:
uint8_t  io[0x400];       // gIoMem
uint16_t bgPltt[256];     // gBgPltt
uint16_t objPltt[256];    // gObjPltt
uint16_t oam[0x200];      // gOamMem
uint8_t  vram[0x18000];   // gVram
```

Record size = 64 + 0x400 + 512 + 512 + 0x400 + 0x18000 + the clip block
(`PortPpuMzm_GetClipRecordBlockSize()`, **256 bytes** as of 'MZM6' -- 28 bytes
of scalars then a 17x12 clip grid; the scalars are camera x/y, Samus x/y,
clipdata w/h, `gMainGameMode`, Samus pose, screen-origin x/y, area, room,
`gCurrentCutscene`, and the montage-cutscene stage; then 12 int16 words on
how the WIDE view was worked out: the renderer's slide x/y, room masks
left/right/top/bottom and flags (bit 0 widened, 1 leaving a room, 2
following the door tunnel), `gSubGameMode1`, `gColorFading` type and stage,
the door tunnel's phase (0 none, 1 over the old room, 2 sliding) and its
x) = 101,696 bytes. The WIDE words were appended without a magic bump:
nothing before them moved, and every parser finds the stride by scanning
for the magic, so 232-byte-block recordings still read. The
block is kept a multiple of 4 (a `_Static_assert` enforces it) so that
`N * recordSize` file offsets stay on a 4-byte grid -- 'MZM5' was 230 bytes
and off-grid, which made a byte-scanner necessary. If any of those extern
arrays -- or the clip block -- change size, recompute this and update
`PlatformGpu3DS_RecordTick` and this doc together. Rather than hardcoding it,
the safe way to get the stride from a fetched file is to scan (stepping by 2)
for the second header magic at least `0x18000` bytes in (a word inside VRAM
can read as a magic by chance, so the distance floor matters). To split a
fetched `mzm-rec.bin` into per-sample dumps for analysis:

```python
REC_SIZE = 64 + 0x400 + 512 + 512 + 0x400 + 0x18000 + 232  # 'MZM6'
with open('mzm-rec.bin', 'rb') as f:
    data = f.read()
n = len(data) // REC_SIZE
print(f"{n} samples")
for i in range(n):
    rec = data[i*REC_SIZE:(i+1)*REC_SIZE]
    (magic, frame, pose, caf, wjt, suit, suitMisc, unk22,
     frameUs, drawX100, procX100, sprites, affine,
     drawCount, blendTransitions, packed) = struct.unpack_from('<16I', rec, 0)
    visibleSprites = packed & 0xFF
    scissorPasses = (packed >> 8) & 3
    hazeTiles = packed >> 16
    io = rec[64:64+0x400]
    bgpltt = rec[64+0x400:64+0x400+512]
    objpltt = rec[64+0x400+512:64+0x400+1024]
    oam = rec[64+0x400+1024:64+0x400+1024+0x400]
    vram = rec[64+0x400+1024+0x400:]
    if frameUs > 17000:
        print(f"sample {i}: OVER BUDGET {frameUs}us "
              f"gpu={drawX100/100:.2f}+{procX100/100:.2f}ms "
              f"quads={drawCount} flushes={blendTransitions} "
              f"spr={visibleSprites} aff={affine} passes={scissorPasses}")
    # ... decode DISPCNT/OAM/tiles/palette the same way as the screen dump
```

Cross-reference `pose`/`currentAnimationFrame` against the `SamusPose` enum
(`include/constants/samus.h`) and the relevant `SamusXxxGfx`/`SamusXxx`
function in `src/samus.c` to know what the game logic *should* be doing at
that exact sample, then compare against what the reconstructed VRAM/OAM/
palette actually show.

## Frame-time recorder (`mzm-perf.bin`)

A sibling of the scene recorder for performance work only. The scene
recorder writes ~100KB per sample to the SD card (~1.5MB/s sustained), which
by itself pushes frames from 16.7ms to 70-200ms -- its own frame-time
numbers are then measuring the recorder, not the game. This one keeps a
40-byte entry per emulated frame in a RAM ring (no SD I/O, no screenshots
while running) and flushes on stop.

**Do not run both at once.** A `mzm-perf.bin` captured with the scene
recorder active is worthless; that is exactly how the 2026-09-04 capture was
lost.

Toggled from the tools menu. ~1 minute of capacity (3600 frames at 60 FPS,
~144KB of linear heap); it flushes and stops on its own at the cap.

```
struct PerfFileHeader {  // 16 bytes, at offset 0
    uint32_t magic;       // 'MZP7' = 0x37505A4D little-endian
    uint32_t sampleSize;  // sizeof(PerfSample) -- stride from this, don't hardcode
    uint32_t sampleCount;
    uint32_t reserved;    // zero
};
struct PerfSample {      // 148 bytes, sampleCount of them back to back
    uint32_t frameCounter;
    uint32_t durationUs;         // wall clock of the frame that just ended.
                                 // ~16675 = on budget; ~33350 = one vblank missed
    uint32_t spriteCount;        // non-disabled OAM entries (~always 128, see below)
    uint32_t visibleSpriteCount; // of those, actually intersecting 240x160
    uint32_t affineSpriteCount;
    // -- GPU cost (citro3d counters) --
    uint32_t drawingTimeX100;    // C3D_GetDrawingTime(), hundredths of ms
    uint32_t processingTimeX100; // C3D_GetProcessingTime(), hundredths of ms
    // -- CPU cost of the GPU renderer itself, which the two counters above
    //    do NOT cover. This is the half that scales with the CPU clock, so
    //    it is what an Old3DS (268MHz against a New3DS's 804MHz, same
    //    PICA200 at the same clock) would suffer three times over. --
    uint32_t cpuTileX100;        // VRAM reads + tile cache hash/decode + sort
    uint32_t cpuUploadX100;      // atlas texture upload (blocking transfer)
    uint32_t cpuDrawX100;        // draw-call submission, every eye
    // -- what the frame drew --
    uint32_t drawCount;          // draw calls, summed over every eye drawn
    uint32_t bgItems;            // collected BG tile quads, ONE eye
    uint32_t objItems;           // collected OBJ subtile quads, ONE eye
    uint32_t blendTransitions;   // opaque<->alpha switches, each a broken batch
    uint32_t rendererFlags;      // bits 0-7   scissor passes (2 = a window splits
                                 //            the draw order, so the whole order
                                 //            is submitted twice)
                                 // bit  8     window active
                                 // bit  9     haze active
                                 // bits 10-11 eyes rendered (2 = 3D slider up)
                                 // bits 16-31 haze tile count
    uint32_t captureFlags;       // the settings the capture was taken under:
                                 // bits 0-1   display style (0 = PIXEL PERFECT)
                                 // bits 2-3   aspect ratio (2 = STRETCH)
                                 // bits 4-10  3D slider x100, 0..100
                                 // bit  11    FPS overlay on
                                 // bit  12    HUD outside the frame
                                 // bit  13    GBA bezel on
                                 // bits 14-16 GBA screen-FX grade (0 = off)
                                 // bit  17    Old3DS profile forced
                                 // bit  18    frame drawn by the GPU renderer
                                 //            (clear = CPU fallback, so the
                                 //            draw-call census is stale)
    uint32_t drawnPixels;        // 'MZP4'+: device pixels the quads covered, all eyes
    uint32_t gpuWaitX100;        // 'MZP5'+: CPU time C3D_FrameBegin waited for the
                                 // GPU to finish the previous frame. The renderer
                                 // collects before that wait (it overlaps the GPU),
                                 // so this is the GPU work the CPU could not hide.
    uint32_t phaseX100[11];      // 'MZP6'+ (8 entries in 'MZP6'): CPU time per phase over this frame,
                                 // 1/100 ms (PerfPhase, platform_gpu_3ds.h):
                                 // 0 game logic (every tick since the last
                                 //   presented frame, VBlank callback included)
                                 // 1 renderer VRAM change-stamp pass
                                 // 2 renderer OAM walk
                                 // 3 renderer BG layers (+ haze BG3)
                                 // 4 renderer sort, diagnostics, atlas flush
                                 // 5 bottom screen tick + redraw
                                 // 6 screen FX + C3D_FrameEnd
                                 // 7 C3D_FrameSync, waiting for the display
                                 // 8-9 ('MZP7') draw submission split (part
                                 //   of cpuDrawX100): the eyes' item loops,
                                 //   the layer-map cell redraw. Captures from
                                 //   before the block passes were removed hold
                                 //   the 32x32 / 16x16 block passes here.
                                 // 10 ('MZP7') per-tile pass (part of phase 3)
    uint32_t counts[8];          // 'MZP7': work over this frame -- times in
                                 // us: layer maps finding/re-checking cells
                                 // whose tiles or palette changed; layer maps
                                 // in all (that included); CollectBgLayer in
                                 // all (that included); the haze BG3 (older
                                 // captures: the 32x32 / 16x16 block lookups
                                 // and decodes);
                                 // tile lookups, decodes; positions the
                                 // per-tile pass examined; WIDE entries rebuilt
                                 // from the room's block map
};
```

Everything from `drawingTimeX100` down is **backfilled** in
`PlatformGpu3DS_EndBottom` rather than written by the sampling tick: the
tick runs before the frame is rendered, so reading those counters there
recorded the *previous* frame's cost against this frame's duration. The tell
that this was happening, in the first capture: 2-vblank frames reported
lower GPU times than 1-vblank ones. A frame that is sampled but never
presented keeps zeros in those fields.

'MZP6' has 8 phases and no `counts`, 'MZP5' lacks `phaseX100`, 'MZP4' also `gpuWaitX100` and 'MZP3' also `drawnPixels`; the fields before
them are unchanged. 'MZP2' was this without `captureFlags`, which made a set of captures
impossible to tell apart afterwards: the 2026-09-04 round taking four of
them (3D on/off against display style) to ask whether cost scales with
pixels or with quads could not say which capture used which style, and
inferring it from the frame cost would have been circular -- the cost is the
thing under measurement. Before that, 'MZP1' was the same idea with a 40-byte sample, no CPU timings, no item
split and no eye count; before that there was a headerless array of 16-byte
samples with no magic at all. Check the magic, and stride by `sampleSize`.

### Reading it

```python
import struct
d = open('mzm-perf.bin', 'rb').read()
magic, size, count, _ = struct.unpack_from('<4I', d, 0)
assert magic == 0x37505A4D, 'not an MZP7 perf capture'
for i in range(count):
    (frame, us, spr, vis, aff, drawX, procX, tileX, upX, cpuDrawX,
     quads, bg, obj, flushes, flags, cap, pixels, waitX) = struct.unpack_from('<18I', d, 16 + i * size)
    print(f"{frame} {us:6d}us gpu={drawX/100:5.2f}+{procX/100:5.2f} "
          f"cpu={tileX/100:5.2f}+{upX/100:5.2f}+{cpuDrawX/100:5.2f}ms "
          f"quads={quads:4d} (bg={bg} obj={obj}) eyes={(flags >> 10) & 3} "
          f"flushes={flushes:3d} spr={vis:3d} passes={flags & 0xFF} "
          f"style={cap & 3} slider={(cap >> 4) & 0x7F} gpuWait={waitX/100:5.2f}ms")
```

### Reading the numbers

`durationUs` is paced by `C3D_FrameSync()` in `PlatformGpu3DS_EndBottom`, so
it quantises to whole vblanks: a frame is either ~16.7ms or ~33.4ms, never
in between. A steady `33, 17, 16.5, 33, 17, 16.5, ...` cadence is 45 FPS and
means the frame is landing *just* over the 16.67ms budget, not that
something hitched. Sum `drawingTime + processingTime` and compare against
16.67 to confirm the GPU is what's over.

**The sprite counts, and why there are two.** MZM does not use the OBJ
disable flag; it parks unused slots offscreen at Y=0xFF. So `spriteCount`
(non-disabled entries) reads 128 in practically every frame of every room
and tells you nothing -- `visibleSpriteCount` is the one that correlates
with cost. Before 2026-09-04 the census also had a wrong disabled test
(`attr0 & 0x0300 == 0x0100`, which is affine-single-size, not disabled),
which is why every sample in every older capture reads `spriteCount=128,
affineSpriteCount=0`. Those two fields are unusable in recordings made
before that date.

## Renderer comparison harness (GPU vs CPU PPU)

The port has two renderers: the PICA200 tile renderer (`port_gpu_renderer.c`)
and the software scanline PPU (`port/ppu/src/mode1.c`). Only the second one
can run on a PC, and only the second one is decompilation-accurate by
construction -- which makes it the **oracle** for the first.

The insight that makes this cheap: **one screen dump already writes both
halves of the comparison at the same instant**, sharing one set number.

| From one press | What it is |
| --- | --- |
| `mzm-dump-NN-left.rgb` | what the GPU renderer actually put on screen |
| `mzm-dump-NN-io.bin` + `-vram` + `-bgpltt` + `-objpltt` + `-oam` | the GBA PPU state it drew that from |

So a dump **validates itself**: render its own state through the CPU PPU and
diff against its own screenshot. There are no golden images to store or keep
up to date, and no need to reproduce a scene frame-exactly -- which is what
makes this usable at all, since you cannot land on the same frame twice by
hand.

```sh
make -C platform/3ds rec-render          # build the reference renderer, once
python3 tools/compare_render.py <dir-with-a-fetched-dump-set>
```

Output per set: `OK`, or the differing pixel count, the worst per-channel
delta, a bounding box, and a `compare-NN.png` triptych of **reference |
console | diff**. The bounding box is what makes a failure diagnosable in
one look -- a scrambled sprite lands in a small box, a palette problem
covers everything.

### Capture conditions

The script checks these and refuses rather than reporting nonsense:

- **3D slider at 0.** Every stereo offset in the GPU renderer is
  `slider3d * TierPx(tier)`, so at 0 they all vanish and the two renderers
  are comparable at all. The CPU PPU has no stereo to compare against.
- **Display style PIXEL PERFECT (1:1).** Otherwise the GPU output is scaled
  1.5x and filtered and every pixel differs. Detected via the pillarbox.
- **FPS overlay and GBA bezel off.** Both are drawn onto the top target by
  the port and have no counterpart in a GBA frame.
- **Colour correction off.** It is a display preference applied by both
  renderers from the same config; the reference deliberately leaves it out,
  so leave it out on the console too. A small delta over nearly every pixel
  is the signature, and the script says so when it sees it.

### What it does and does not cover

Covers the **2D composition**: layer order, BG and OBJ priorities, palettes,
blending, windows, tile decoding. That is where a bug from batching,
occlusion culling or caching a layer in a render target would land, and it
is exactly the shape of issue #17 -- where the CPU reconstruction of a frame
showed a correct Samus and the console showed a scrambled sprite. That
comparison was done by hand, for weeks.

Does **not** cover stereo, because the oracle has none. Stereo splits into
two halves that are checkable without it:

- Depth *assignment* (which plane each layer lands on) is already unit
  tested -- `platform/3ds/tests/stereo_depth_test.c`, 26542 checks against
  GBATEK compositing rules derived independently of the code.
- Depth *application* needs no reference image, only properties: at slider 0
  the two eye dumps must be **byte-identical**, and at slider > 0 each tier
  must shift rigidly by a whole number of pixels. Diff
  `mzm-dump-NN-left.rgb` against `-right.rgb`, not against an oracle.

### Baseline, 2026-09-04

First run over ten dumps taken around the game (New3DS, slider 0, pixel
perfect):

- **Six of ten are pixel-identical to the CPU PPU** across all 38400 pixels.
- Three (`02`, `03`, `08`) differ in small amounts over a tinted or lava
  region: whole-frame red heat tint, and animated lava bands. Mean per-channel
  delta 8-10, visually indistinguishable side by side. Consistent with the
  two renderers rounding a blend differently, not with a compositing bug.
- One (`04`) has **17 pixels** differing in a 3x11 box, and this one is a
  real placement divergence: `console(112,51)` holds what the reference has
  at `(111,51)`, and so on down the column -- a small sprite drawn one pixel
  to the right. Worth chasing on its own; it is the kind of off-by-one that
  a whole-frame eyeball test will never find.

Treat that as the baseline. What matters from here is that the set does not
grow and that no divergence gains area.

Two calibration bugs had to be fixed before those numbers meant anything,
both in the harness rather than the port, and both worth knowing about:

- The `.rgb` dumps are BGR (see **Reconstructing the screenshot**). Before
  that was found, all ten dumps "differed" over 40-100% of the frame.
- The two renderers expand GBA 5-bit colour to 8 bits differently: mode1
  shifts (`v << 3`, 31 -> 248), the GPU path scales to full range (31 ->
  255). Real, invisible, and enough to flag every pixel. The comparator
  quantises both sides back to 5 bits, which is lossless for this purpose
  since GBA palettes and GBA blending are both 5-bit. `--exact` shows the
  raw bytes.

### Expect known divergences on the first run

The GPU renderer approximates some things on purpose (affine OBJ shear is
decomposed to rotation plus axis-aligned scale, for one -- see
`CollectSprite`). The first run over a fresh set of dumps establishes what
the *current* divergences are; from then on the thing that matters is that
the set does not grow. Do not read the first non-zero result as a
regression.

### Rendering a recording instead

`rec_render` also takes a scene recording, for reference frames of a whole
sequence (there is no matching console screenshot per sample, so this is for
looking, not diffing):

```sh
platform/3ds/build/rec_render rec mzm-rec-01.bin out/      # every sample
platform/3ds/build/rec_render rec mzm-rec-01.bin out/ 12   # just sample 12
```

## Warp / teleport (new -- no combo equivalent)

Behind the **TELETRANSPORTE** / **WARP** row of the tools menu. Exists so a
rendering bug that only reproduces in one specific room doesn't cost a full
replay to that room after every new CIA install.

Jumps are addressed by **(area, door id)**, never by a room number plus
coordinates. `RoomReset` (`src/room.c`) derives everything from
`gLastDoorUsed`: `gCurrentRoom = pDoor->sourceRoom`, and Samus's position
from the door's `xStart`/`yEnd`/`xExit`/`yExit` -- exactly what a real door
transition does, so Samus always lands somewhere legal. Setting a room
number plus a guessed x/y instead (what the `PORT_LINUX_DIAG_WARP_TO_DEOREM`
block in `src/agbmain.c` does, with coordinates hand-tuned for one room)
drops her wherever those coordinates happen to land in the new room's
geometry.

| Row | What it does |
| --- | --- |
| `AREA  < name >` | Steps the target area. Resets the door index if the current one is out of range for the new area. |
| `PUERTA < n / max >` | Steps the target door. The next row shows which room that door leads into, so a room can be found by stepping doors without knowing any door ids up front. |
| `IR A ESA PUERTA` | Warps to the selected (area, door). |
| `GUARDAR PUNTO AQUI` | Records the door Samus last came through (`gCurrentArea` + `gLastDoorUsed`), i.e. "bring me back to this room". Persisted to `states/mzm-warp-point.txt`, so it survives a reflash. |
| `IR AL PUNTO GUARDADO` | Warps to that saved point. |

The two lines under the rows show the saved point and where Samus is right
now (`AHORA: <area> SALA <n>`), so the spinner has something to aim at.

**Where the jump actually happens:** the menu only raises a pending flag.
`PortPpuMzm_DebugApplyPendingWarp` (`port_ppu_mzm.c`) is called from
`src/agbmain.c` at the top of its main loop, and only fires while really in
gameplay (`GM_INGAME` / `SUB_GAME_MODE_PLAYING`). It cannot run from the
touch handler itself: that runs inside `Port_Bios_Halt`, which
`src/transfer.c` also calls mid-frame, so resetting `gSubGameMode1` from
there could land in the middle of a room's own update. A request raised
from a pause screen stays pending and fires as soon as play resumes.

## FTP server, install and remote control (DEBUG -> HERRAMIENTAS -> FTP SERVER)

A small FTP server inside the game, so files can be fetched or dropped while it keeps running:
no closing the game, no FBI or ftpd. The cell shows the console's IP (the port is always 5000,
the same as FBI and ftpd, so `tools/build_3ds.py --ftp` and the usual client bookmarks work);
its title says `CLIENT ON` while one is connected. It needs Wi-Fi; the cell says why when it
cannot start. The side button starts or stops it.

An FTP server left on starts again at the next launch (`debug_ftp` in `mzm3ds.ini`, debug builds
only), retrying every ~2 s until Wi-Fi is up (`WAITING WI-FI`; tapping the button cancels it).
The SD log keeps its own rule: off until switched on.

- Anonymous: any user name and password. `/` is the root of the SD card; the game's files are in
  `/3ds/Metroid Zero Mission 3DS/` (`debug/`, `states/`, `mzm3ds.ini`).
- Passive mode only (PASV and EPSV, data ports 5001-5064), one client at a time.
  FileZilla, WinSCP, lftp and curl work:

```sh
curl ftp://<3ds-ip>:5000/3ds/Metroid%20Zero%20Mission%203DS/debug/
curl -O ftp://<3ds-ip>:5000/3ds/Metroid%20Zero%20Mission%203DS/debug/mzm-debug-01.log
```

The server thread runs on core 1 (when the app has a share of it) below the game's priority and
sleeps on the network, so it costs nothing while idle. It shares the socket service with the
updater (`net_3ds.c`). Its lines reach the SD log through the logic thread.

### Installing a build over FTP

`SITE INSTALL <path>` installs a CIA of this game that is already on the SD card over the running
game and relaunches into it (the FTP server comes back on with it). `python3 tools/build_3ds.py
--install IP` builds, uploads to `/3ds/Metroid Zero Mission 3DS/update/dev.cia` and installs in
one go; the interactive menu offers it as "Instalar sobre el juego en marcha". Unsaved progress is
lost. While a `.cia` arrives and while it installs, the bottom screen shows a box in the middle
(`NEW BUILD`: `RECEIVING THE INSTALLER...`, then `INSTALLING - THE GAME WILL RESTART`) on any tab.
A CIA of another title is refused. The first build with this feature has to arrive by FBI or
ftpd as before.

### Remote control

While the server runs, the PC drives the game through SITE commands
(`platform/3ds/source/debug_link_3ds.h`). `tools/mzm-link/mzm-link.py` is the client (standard
library only):

```sh
tools/mzm-link/mzm-link.py wait                 # until the game answers (after an install)
tools/mzm-link/mzm-link.py state                # frame=... mode=04 area=0 room=... energy=99/99 ...
tools/mzm-link/mzm-link.py press LEFT 120       # hold LEFT 120 logic frames and wait for them
tools/mzm-link/mzm-link.py keys A+B 2           # buttons by their 3DS names, joined by +
tools/mzm-link/mzm-link.py touch 160 120        # a bottom-screen tap
tools/mzm-link/mzm-link.py savestate            # a new state; `savestate 2` saves over number 2
tools/mzm-link/mzm-link.py loadstate            # the newest; `loadstate 3` loads number 3
tools/mzm-link/mzm-link.py states               # number=area/room for each
tools/mzm-link/mzm-link.py shot                 # /tmp/mzm-shot/top.png and bottom.png
tools/mzm-link/mzm-link.py peek ewram:0 16      # GBA EWRAM/IWRAM by offset, or a symbol: peek gSamusData 16
tools/mzm-link/mzm-link.py switch "FRAME SKIP"  # RENDERER, DEPTH TINT, OLD 3DS PROFILE, LOG BUFFERING
tools/mzm-link/mzm-link.py run tools/mzm-link/examples/walk-left.txt
```

Buttons are added to the real ones and counted in logic frames, so a held direction moves Samus
exactly as far each time (they go through the button mapping, like the real ones). Touches,
screenshots, dumps, states and writes run on the logic thread, once per frame; the reply waits for
them. Save states follow their own rules (`docs/3ds-save-states.md`): saving needs real gameplay.
Reads and writes check that the memory is mapped first, so a wrong address answers `550`.

## Known findings so far (#20, frame rate)

> The live work order for this -- what is done, what is next, and the
> hypotheses that were tried and disproved -- is
> [3ds-renderer-perf-plan.md](3ds-renderer-perf-plan.md). This section is
> the measurement record; that one is the plan.


From the 2026-09-04 captures (`mzm-rec-3/4/5.bin`, area 2 rooms 1 / 39 / 43):

- The 45 FPS is **not** a hitch. It is a steady `33ms / 17ms / 16.5ms`
  cadence -- two frames on budget, one missing a vblank. Three frames in
  66ms is exactly 45 FPS.
- It is GPU-bound, and sustained: `drawingTime + processingTime` was 17-24ms
  in the slow rooms against ~9ms in the room that held 60 FPS.
- **Sprites are ruled out.** The slow room had *fewer* visible sprites than
  the fast one (9 against 37) and no affine sprites at all, which kills the
  "Skree explosion affine sprites" hypothesis the perf fields were
  originally added for.
- `BLDCNT` looked like it tracked it (`0x3E41`, alpha blend with BG0 as
  first target, in both slow rooms against `0x3F40` -- alpha mode with an
  empty first-target mask, i.e. no actual blending -- in the fast one) and
  that turned out to be **coincidence across a sample of three rooms**. The
  first instrumented capture killed it: `blendTransitions == 4` in all 429
  frames, `scissorPasses == 1`, haze inactive. Blending, the window pass and
  the haze are all ruled out. Don't re-derive this hypothesis from `BLDCNT`
  alone; measure the flush count.
- What the instrumented capture did show: cost is **flat**, not spiky.
  draw+proc median 17.19ms against a 16.675ms budget, min 15.44, max 26.21,
  with 53% of frames over. Nothing hitches; half the frames just miss, and
  because `C3D_FrameSync` quantises to whole vblanks each miss costs a full
  33ms. 295 single-vblank frames against 133 double = 45.7 FPS. The gap to
  close is ~1ms typical, ~2ms worst case.
- `drawCount` was 4276-4774 per frame, and that is **more than one eye can
  possibly produce**: the per-eye ceiling is 4 BG layers x 21 rows x 32
  columns = 2688 quads plus OBJ subtiles (~50-200 in these rooms). Simulating
  `CollectBgLayer`'s own opaque-tile test offline against the recordings
  gives 2301 / 988 / 980 BG quads for rooms 43 / 1 / 39. So that capture was
  rendering **both eyes** -- 3D slider up, whole scene submitted twice --
  which the eye count in `rendererFlags` now records instead of leaving it
  to be inferred.
- **Measured, 2026-09-04, same room and same movement in both captures**
  (`mzm-perf-01.bin` 3D on, `-02.bin` 3D off, 894 / 1043 presented frames):

  | | 3D on | 3D off |
  |---|---|---|
  | FPS | 44.9 | **59.8** (1041 of 1043 frames on a single vblank) |
  | eyes | 2 | 1 |
  | quads/frame | 4572 | 2281 |
  | GPU (draw+proc) | 17.55 ms | 10.25 ms |
  | CPU renderer | 9.26 ms | 5.82 ms |
  | -- tile decode | 2.39 ms | 2.40 ms |
  | -- draw submission | 6.71 ms | 3.33 ms |

  So on a New3DS with the slider down the port already holds 60. The entire
  45 FPS story is the second eye, and it is a **pure duplicate of submission
  and rasterisation**: tile decode is identical in both (collection happens
  once per frame, not once per eye) while submission and GPU time double.

  Do not add GPU and CPU time together and compare against 16.675 -- they
  overlap. In the 3D-off capture 46% of frames have a sum over budget while
  only one frame in 1043 actually missed a vblank.

- **Cost per quad, from the two densities: ~3.2us of GPU plus ~1.5us of CPU
  submission.** That is the number to design against. An 8x8 GBA tile is a
  12x12 quad on screen, i.e. 144 pixels, so 3.2us works out to ~22ns per
  pixel -- orders of magnitude off what the PICA200 can fill. The cost is
  **per-quad overhead, not fill rate**, which points the optimisation at
  batching (fewer, larger quads) ahead of culling (fewer, same-size quads).
  Fitting the 3D-on case in budget needs roughly 490 quads' worth.

- **Old3DS, answered with numbers rather than a guess:** the GPU is the same
  PICA200 at the same clock, so its ~10.25 ms would not change, but the CPU
  renderer's 5.82 ms is at 804MHz. At an Old3DS's 268MHz that is ~17.5 ms of
  CPU **alone**, past the whole 16.675 ms budget before the GPU does
  anything -- and the port's Core1 grant is New3DS-only, so the real figure
  is likely worse than that naive 3x. 60 FPS on an Old3DS is not reachable
  with this renderer; it needs the per-quad work cut on both sides at once,
  which again means batching, plus caching scrolling layers in render
  targets to attack the 2.4 ms of tile decode.

- One genuinely reassuring result: that 2.4 ms of tile decode held steady
  while the capture ran left, right and jumping, i.e. scrolling on both
  axes, which is the worst case for atlas cache churn. The tile cache is
  doing its job.

- Careful with room 1 (`rec-3`): `drawingTime` alone was 13-19ms there with
  only 988 BG quads and 51 OBJ subtiles, the lightest scene of the three.
  Quad count cannot be its limiter. That room has a *different* problem
  (fill rate or texture-bound is the guess), so do not assume one fix covers
  both.
- Do not fit a per-quad cost from a single capture: the quad range within
  one is ~11%, far too narrow to condition a slope.

Two crashes were also root-caused from Luma ARM11 dumps in the same session,
both the same shape -- a pointer the GBA could read at address 0 (the BIOS
returns junk, no fault) that faults on the 3DS, where page 0 is unmapped:

- `SpriteUtilUpdateSubSprite1Anim` with `gSubSpriteData1.pMultiOam == NULL`,
  reached from `ImagoCocoon` after `ImagoCocoonInit` took its
  already-dead-plus-Ridley-demo early return.

  This took three attempts, and the way it failed is the lesson:

  1. Guarding `src/sprite_util.c` alone was not enough -- the next build
     aborted one instruction chain later in `ImagoCocoonSyncSprites` (dump
     51), which repeats the same `pMultiOam[...]` deref locally instead of
     going through sprite_util.
  2. Guarding four callers' tails was not enough either -- dump 52 hit
     `RidleySyncSubSprites`. Those four had been picked from a
     `grep pMultiOam src/` whose output had been **truncated by `head -30`**,
     so ridley, kraid, mecha_ridley, mother_brain, imago_larva, tangle_vine
     and unknown_item_chozo_statue were never in the list -- which is
     precisely where warping into a boss room lands.
  3. There are **twelve** files and 23 dereferencing functions. Every one of
     them now carries a guard immediately before its first dereference,
     inserted mechanically rather than by reading the list again.

  If this ever comes back, do not guard one more caller: re-run the search
  without a `head` and check that
  `grep -rn 'pMultiOam\[' src/` has a `pMultiOam == NULL` above every hit.
- `SamusDraw` with an environmental-effect slot whose `type` was set but
  whose `pOamFrame` was still NULL. Guarded in `src/samus.c`.

### Where the headroom is

Not "the port reuses the decomp's structure" -- the decompiled game logic is
cheap (it ran on a 16MHz ARM7). The cost is in code this project owns,
`port_gpu_renderer.c`, and it is the cost of reproducing a hardware tile
compositor with a triangle rasterizer: one textured quad per 8x8 tile per
layer, where the GBA PPU did the same job in dedicated silicon for free.
Plus ~4.5x the GBA's pixel count (2 eyes of 360x240 against one 240x160).

It is written the direct way, which was the right call to get it working,
and that leaves real room. In rough order of expected payoff:

1. **The second eye redraws everything.** Collect and submit run again per
   eye when the only difference between them is a per-depth-tier horizontal
   offset.
2. **No occlusion culling.** Tiles fully covered by an opaque
   higher-priority tile are still submitted. In room 43, BG2 and BG3
   contribute 672 and 693 quads, much of it behind opaque layers. Needs a
   coverage mask over the 32x21 screen cells, walked front-to-back, and a
   "tile is FULLY opaque" test (the current one is "has any opaque pixel").
   Must never cull what sits under an alpha-blended first-target layer.
3. **One quad per tile.** Runs of horizontally adjacent tiles from the same
   atlas row could go in one quad.
4. **A BG layer that only scrolls is re-collected every frame.** It could
   live in a render target and be blitted with an offset -- the RTT
   machinery already exists and is proven by the haze path.

Resolving a dump: parse it (magic `DEADC0DE DEADCAFE`, then a 40-byte
header, then the registers -- `pc` is register 15) and run the addresses
through `arm-none-eabi-addr2line -f -e platform/3ds/mzm-3ds.elf`, keeping
the `.elf` from the build that was actually installed.

## Known findings so far (#17)

- The green/cyan palette flash right after death starts is **correct** --
  it comes from `sSamusPal_Generic_Dying`/`sSamusPal_PowerSuit_Dying`
  (`src/data/samus/samus_palette_data.c`), consumed by `SamusUpdatePalette`
  (`src/samus.c`), which is a byte-matching decompiled function (verified
  against the retail ROM) -- not a decomp bug.
- The captured frame that showed this had `DISPCNT=0x5000` (only `WIN1`+
  `OBJ` enabled, no BG layers at all) with `WIN1H=0xF0`/`WIN1V=0xA0` --
  i.e. WIN1 covering the *entire* screen, not clipping anything (just
  gating BLDCNT per layer). So for that specific frame, the GPU renderer's
  window-clip/scissor path (`port_gpu_renderer.c`) never actually engages --
  ruled out as the cause for *that* frame, though it may still be relevant
  for other frames later in the sequence (not yet captured as of this
  writing).
- `gSuitFlashEffect` (`src/in_game_cutscene.c`, `src/transparency.c`) is
  **not** used anywhere in `src/samus.c` -- it's a separate cutscene-only
  mechanism (e.g. getting the Fully Powered Suit), not part of the death
  sequence. Don't assume it's involved without checking again.
- Still unconfirmed: the actual moment the sprite becomes "scrambled/
  unrecognizable" (per the reporter's description of real hardware
  behavior: crouch -> green flashes -> stretch back -> suit disappears to
  reveal Zero Suit, background fades black-to-white in parallel). The single screen dump only ever caught the early, correct-looking flash frames --
  this is exactly why the scene recorder was built. Next step when
  resuming this investigation: get an `mzm-rec.bin` covering the whole
  death sequence, decode every sample, and find where the reconstructed
  image first looks wrong.
