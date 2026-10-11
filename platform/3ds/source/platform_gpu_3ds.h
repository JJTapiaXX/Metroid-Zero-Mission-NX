#ifndef PLATFORM_GPU_3DS_H
#define PLATFORM_GPU_3DS_H

#include <stdbool.h>
#include <stdint.h>

/* Forward-declared instead of pulling in <citro3d.h> here -- only the tile
 * GPU renderer needs the real type, and it already includes citro3d.h
 * itself. Keeps this header includable from files that don't want the full
 * 3ds/citro3d dependency. */
struct C3D_RenderTarget_tag;

typedef struct PlatformGpu3DSStats {
    uint64_t frames;
    uint64_t frameBeginFailures;
    uint64_t topTransfers;
    uint64_t bottomTransfers;
    uint64_t bottomTargetDraws;
    uint64_t bottomTargetReuseSkips;
    uint64_t boundedFlushBytes;
    uint32_t linearHeapBytes;
    uint32_t c2dFlushBytes;
    uintptr_t c2dFlushAddress;
    uintptr_t topUploadAddress;
    uintptr_t bottomUploadAddress[2];
    float drawingTime;
    float processingTime;
} PlatformGpu3DSStats;

bool PlatformGpu3DS_Init(bool old3dsProfile);
/* Runtime override for the debug New3DS/Old3DS-profile toggle. */
void PlatformGpu3DS_SetOld3DSProfile(bool on);
float PlatformGpu3DS_Get3DSlider(void);
uint32_t* PlatformGpu3DS_TopBuffer(void);
uint32_t* PlatformGpu3DS_TopRightBuffer(void);
uint32_t* PlatformGpu3DS_BottomBuffer(unsigned index);
void PlatformGpu3DS_BeginTop(const uint32_t* pixels, unsigned width);
/* Must bracket every actual GPU submission call (BeginTop.../EndBottom) made
 * from more than one thread -- see the doc comment on sGpuSubmitLock in
 * platform_gpu_3ds.c for why. */
void PlatformGpu3DS_SubmitLock_Acquire(void);
void PlatformGpu3DS_SubmitLock_Release(void);

void PlatformGpu3DS_DrawStatusText(float x, float y, float scale, const char* text);
void PlatformGpu3DS_DrawFpsOverlay(float eyeXOffset);
/* Once per game logic tick (the key poll): the overlay's game-rate number. */
void PlatformGpu3DS_OverlayLogicTick(void);
void PlatformGpu3DS_BeginTopStereo(const uint32_t* leftPixels, const uint32_t* rightPixels, unsigned width);
/* Returns true only when a Citro3D frame was active and submitted. */
bool PlatformGpu3DS_EndBottom(const uint32_t* pixels, bool changed);

/* GPU tile-renderer integration (port_gpu_renderer.c): opens the frame like
 * PlatformGpu3DS_BeginTopStereo does (C3D_FrameBegin) but does NOT upload or
 * draw a CPU-composited pixel buffer -- the caller draws its own scene
 * (tile/sprite quads) directly into the shared top-screen render targets
 * below, then still finishes the frame with PlatformGpu3DS_EndBottom as
 * usual. Deliberately reuses this module's existing targets/citro3d/citro2d
 * global state instead of the tile renderer creating its own -- two
 * independent C3D_RenderTargetCreate/gfxSet3D sets both pointed at GFX_TOP
 * would double VRAM usage and race over which one actually reaches the
 * screen. */
bool PlatformGpu3DS_BeginTopSceneGpu(void);

/* Per-phase CPU time for the perf recorder (mzm-perf-NN.bin, 'MZP6'): each
 * phase accumulates over one presented frame -- game logic over every logic
 * tick since the last one, so a frame-skipped pair counts both. Ticks come
 * from svcGetSystemTick. Cheap enough to leave on; only the recorder reads it. */
typedef enum {
    PERF_PHASE_LOGIC = 0,     /* game code between two Port_Bios_Halt calls */
    PERF_PHASE_VRAM_DIFF,     /* renderer: VRAM change-stamp pass */
    PERF_PHASE_SPRITES,       /* renderer: OAM walk */
    PERF_PHASE_BG,            /* renderer: BG layers (and the haze BG3) */
    PERF_PHASE_COLLECT_REST,  /* renderer: sort, diagnostics, atlas flush */
    PERF_PHASE_BOTTOM_UI,     /* bottom screen tick + (throttled) redraw */
    PERF_PHASE_FRAME_END,     /* screen FX + C3D_FrameEnd submission */
    PERF_PHASE_VSYNC,         /* C3D_FrameSync: waiting for the display */
    /* Slots 8-9 held the 32x32 / 16x16 block passes (a breakdown of
     * PERF_PHASE_BG) until those were removed; now a breakdown of the draw
     * submission (cpuDrawX100), not of any phase above: */
    PERF_PHASE_DRAW_ITEMS,    /* the eyes' item loops (quads + state changes) */
    PERF_PHASE_DRAW_MAPS,     /* redrawing stale layer-map cells */
    PERF_PHASE_BG_TILES,      /* the per-tile pass (a breakdown of PERF_PHASE_BG) */
    PERF_PHASE_COUNT
} PerfPhase;
void PlatformGpu3DS_PerfPhaseAdd(PerfPhase phase, unsigned long long ticks);

/* Work counts over the same frame, 'MZP7'. */
typedef enum {
    /* Slots 0-3 counted the block passes until they were removed. */
    PERF_COUNT_LM_WALK_US = 0, /* layer maps: finding and re-checking the cells
                                * whose tile pixels or palette changed, us */
    PERF_COUNT_LM_US,          /* layer maps: everything (the above included), us */
    PERF_COUNT_BG_LAYERS_US,   /* CollectBgLayer, all layers (the above included), us */
    PERF_COUNT_HAZE_US,        /* the haze BG3 collection, us */
    PERF_COUNT_TILE_LOOKUPS, PERF_COUNT_TILE_DECODES,
    PERF_COUNT_TILE_POSITIONS, /* positions the per-tile pass examined */
    PERF_COUNT_WIDE_REBUILT,   /* tilemap entries rebuilt from the room's block map */
    PERF_COUNT_COUNT
} PerfCounter;
void PlatformGpu3DS_PerfCountAdd(PerfCounter counter, unsigned count);
struct C3D_RenderTarget_tag* PlatformGpu3DS_GetTopLeftTarget(void);
struct C3D_RenderTarget_tag* PlatformGpu3DS_GetTopRightTarget(void);

/* Rectangle (in 400x240 top-screen pixels) the emulated GBA image occupies
 * for the current display style / aspect. Any output pointer may be NULL. */
void PlatformGpu3DS_GetTopImageRect(int* outX, int* outY, int* outW, int* outH);

/* One-shot diagnostic dump and the start/stop scene recorder, both
 * triggered from the DEBUG tab's tools menu, see platform_gpu_3ds.c. PlatformGpu3DS_RecordTick must be
 * called once per emulated GBA frame (Port_Bios_Halt does this) -- it's a
 * no-op unless recording is active. PlatformGpu3DS_IsRecording is for the
 * on-screen "REC" indicator (port_bottom_ui_3ds.c). */
void PlatformGpu3DS_DumpScreens(void);
/* Both screens as 24-bit BMP files (debug_link_3ds.c, SITE SHOT); false when either failed. */
bool PlatformGpu3DS_SaveShots(const char* topPath, const char* bottomPath);
void PlatformGpu3DS_ToggleRecording(void);
void PlatformGpu3DS_RecordTick(void);
bool PlatformGpu3DS_IsRecording(void);

/* Scene-recorder preset, cycled live from the debug menu (tap the right edge
 * of the SCENE RECORDER cell). Selects sample rate (every Nth frame) and
 * whether samples stream to SD or are held in a RAM buffer flushed on stop.
 * Cycling is ignored while a recording is in progress. */
unsigned PlatformGpu3DS_CycleRecordPreset(void);
const char* PlatformGpu3DS_RecordPresetLabel(void);
/* Basename of the last buffered-mode flush (e.g. "mzm-rec.bin" or
 * "mzm-rec-2.bin"); "" before the first one. */
const char* PlatformGpu3DS_RecordLastFile(void);

/* Perf-only frame-time recorder (debug tools menu, issue #20): samples every emulated
 * frame's duration + OAM census into a RAM buffer with no SD I/O while
 * running (unlike the full recorder above, which slows the game down);
 * flushing to <game folder>/debug/mzm-perf-NN.bin happens on stop. Same tick contract as
 * RecordTick: once per emulated GBA frame from Port_Bios_Halt. */
void PlatformGpu3DS_TogglePerfRecording(void);
void PlatformGpu3DS_PerfRecordTick(void);
bool PlatformGpu3DS_IsPerfRecording(void);

/* Sets up the 3-stage TexEnv pipeline that any texture built with this
 * project's "R,G,B,A in memory order" packing (see mode1.c's ABGR8888
 * comment and Bgr555ToRgba8 in port_gpu_renderer.c) needs in order to
 * display correctly. Not cosmetic: the PICA200 samples a GPU_RGBA8 texture
 * with its components in the REVERSE of that memory byte order (component R
 * == memory byte 3, G == byte 2, B == byte 1, A == byte 0), so a naive
 * single-stage passthrough (GPU_REPLACE) shows the wrong channels swapped
 * into the wrong slots -- correct pixel shapes, badly wrong/washed-out
 * colors. This reconstructs true RGB from the swapped components. Any code
 * drawing a texture built with that packing convention (the CPU-composited
 * top buffer in this file, and the GPU tile atlas in port_gpu_renderer.c)
 * must call this before its draws -- TexEnv state is global GPU state, not
 * per-texture, so it doesn't happen automatically. */
void PlatformGpu3DS_ConfigureAbgrTextureEnv(void);
void PlatformGpu3DS_ResetSolidTexEnv(void);
void PlatformGpu3DS_ShowDumpSavedOverlay(void);
void PlatformGpu3DS_DumpScreens(void);
void PlatformGpu3DS_GetStats(PlatformGpu3DSStats* stats);
void PlatformGpu3DS_InvalidateBottomTarget(void);
void PlatformGpu3DS_Shutdown(void);

#endif
