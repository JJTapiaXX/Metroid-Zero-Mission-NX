#include "platform_gpu_3ds.h"
#include "port_debug_tools.h" /* PORT_DEBUG_TOOLS_ACTIVE */
#include "port_gba_screen_fx.h"
#include "port_gba_bezel.h"
#include "port_wide_view.h"
#include "port_gpu_renderer.h" /* PortGpuRendererDrawStats */
#include "port_debug_files.h"
#include "port_oam_census.h"
#include "port_paths.h"

/* How many captures of each kind to keep on the SD card before the oldest
 * slot is reused. Sized by what one costs: a perf capture is ~154KB and a
 * screen-dump set ~1MB, so ten of each is nothing; a scene recording is up
 * to 32MB, so four. */
#define PORT_KEEP_PERF 10u
#define PORT_KEEP_DUMPS 10u
#define PORT_KEEP_RECORDINGS 4u

/* Slot number of the screen dump currently being written, so the Samus
 * files written from port_ppu_mzm.c land in the same set. Read through
 * PlatformGpu3DS_DumpSetIndex() rather than shared as a header extern,
 * matching how everything else crosses that translation-unit boundary. */
static unsigned sDumpSetIndex = 1u;
unsigned PlatformGpu3DS_DumpSetIndex(void) { return sDumpSetIndex; }

#include <3ds.h>
#include <citro2d.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Implemented in port_ppu_mzm.c, which can safely include structs/samus.h
 * (this file can't: it also pulls in <3ds.h>/<citro3d.h>, whose u32 typedef
 * conflicts with the GBA-port one dragged in by structs/samus.h). */
void PortPpuMzm_DumpSamusState(void);

static C3D_RenderTarget* sTopTarget;
static C3D_RenderTarget* sTopRightTarget;
static C3D_RenderTarget* sBottomTarget;
static C3D_Tex sTopTexture;
static C3D_Tex sTopRightTexture;
static C3D_Tex sBottomTexture;
static Tex3DS_SubTexture sTopSubtexture;
static Tex3DS_SubTexture sBottomSubtexture;
static uint32_t* sTopUpload;
static uint32_t* sTopRightUpload;
static uint32_t* sBottomUploads[2];
static void* sC2dFlushBase;
static size_t sC2dFlushSize;
static bool sFrameActive;
static bool sReady;
static bool sOld3DSProfile;
static bool sBottomTargetValid;
static PlatformGpu3DSStats sStats;
static unsigned sTopPresentWidth = 240;

/* Serializes every actual GPU submission (C3D_FrameBegin/End, citro2d
 * draws) across the two threads that can now issue them: the present
 * thread (port_ppu_mzm.c's Port_PPU_GpuPresentPump, consuming a CPU-
 * rendered frame published by the game-logic thread) and the game-logic
 * thread itself, which still calls straight into this file synchronously
 * for the experimental GPU tile renderer path (PORT_GPU_TILE_RENDERER) and
 * the PORT_PPU_TEST_PATTERN diagnostic -- neither produces a CPU pixel
 * buffer that can be handed off the way the normal path does, so they were
 * left calling BeginTop.../EndBottom inline, same as before the present
 * thread existed. That was fine when everything ran on one thread; once
 * present moved to its own thread, those two call sites became a second,
 * completely unsynchronized caller of citro3d/citro2d (neither of which is
 * thread-safe) racing the present thread's own calls -- confirmed on
 * hardware as a hang (not a crash) exactly at a CPU<->GPU renderer
 * transition: the screen freezes while gameplay and audio (a separate
 * thread untouched by any of this) keep running normally, matching a
 * wedged GPU command queue rather than a crashed thread. */
static LightLock sGpuSubmitLock;

void PlatformGpu3DS_SubmitLock_Acquire(void) { LightLock_Lock(&sGpuSubmitLock); }
void PlatformGpu3DS_SubmitLock_Release(void) { LightLock_Unlock(&sGpuSubmitLock); }

enum {
    TOP_TEXTURE_WIDTH = 512,
    TOP_TEXTURE_HEIGHT = 256,
};

extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;
extern bool Port_Config_GetShowFps(void);
extern int Port_Config_GetFpsPosition(void);
extern int Port_Config_Get3DSAspectRatio(void);
extern int Port_Config_Get3DSDisplayStyle(void);
extern double Port_PPU_3DS_CurrentFps(void);
extern bool Port_PPU_3DS_LastFrameUsedGpu(void);
extern void Port_GpuRenderer_GetLastFrameStats(int* outItems, int* outObjItems, int* outCacheSlots);

enum {
    TOP_ASPECT_LEGACY_WIDE = 0, /* the old stretched WIDE, retired -- Port_Config_Load clamps it to ORIGINAL */
    TOP_ASPECT_ORIGINAL,
    TOP_ASPECT_STRETCH,
    /* Shows the world past the GBA frame (port_wide_view.h). Only the GPU
     * renderer can draw that; a frame this file draws itself is a plain
     * ORIGINAL one. */
    TOP_ASPECT_WIDE,
};

enum {
    TOP_DISPLAY_PIXEL_PERFECT = 0,
    TOP_DISPLAY_SCALED,
    TOP_DISPLAY_BLUR,
};

static const uint8_t* StatusGlyph(char c) {
    static const uint8_t digits[10][7] = {
        { 14, 17, 19, 21, 25, 17, 14 }, { 4, 12, 4, 4, 4, 4, 14 },
        { 14, 17, 1, 2, 4, 8, 31 },     { 30, 1, 1, 14, 1, 1, 30 },
        { 2, 6, 10, 18, 31, 2, 2 },     { 31, 16, 16, 30, 1, 1, 30 },
        { 14, 16, 16, 30, 17, 17, 14 }, { 31, 1, 2, 4, 8, 8, 8 },
        { 14, 17, 17, 14, 17, 17, 14 }, { 14, 17, 17, 15, 1, 1, 14 },
    };
    /* A-Y minus Q/Z (never needed so far) -- extended from the original
     * A,C,D,E,F,G,M,P,S,U,V set to label the debug overlay properly
     * (NEW3DS/OLD3DS, item/obj/cache prefixes) instead of unlabeled
     * numbers -- see PlatformGpu3DS_EndBottom. */
    static const uint8_t letters[24][7] = {
        { 14, 17, 17, 31, 17, 17, 17 }, /* A */
        { 30, 17, 17, 30, 17, 17, 30 }, /* B */
        { 31, 16, 16, 16, 16, 16, 31 }, /* C */
        { 30, 17, 17, 17, 17, 17, 30 }, /* D */
        { 31, 16, 16, 30, 16, 16, 31 }, /* E */
        { 31, 16, 16, 30, 16, 16, 16 }, /* F */
        { 14, 17, 16, 23, 17, 17, 14 }, /* G */
        { 17, 17, 17, 31, 17, 17, 17 }, /* H */
        { 31, 4, 4, 4, 4, 4, 31 },      /* I */
        { 7, 2, 2, 2, 2, 18, 12 },      /* J */
        { 17, 18, 20, 24, 20, 18, 17 }, /* K */
        { 16, 16, 16, 16, 16, 16, 31 }, /* L */
        { 17, 27, 21, 21, 17, 17, 17 }, /* M */
        { 17, 25, 21, 19, 17, 17, 17 }, /* N */
        { 14, 17, 17, 17, 17, 17, 14 }, /* O */
        { 30, 17, 17, 30, 16, 16, 16 }, /* P */
        { 30, 17, 17, 30, 20, 18, 17 }, /* R */
        { 15, 16, 16, 14, 1, 1, 30 },   /* S */
        { 31, 4, 4, 4, 4, 4, 4 },       /* T */
        { 17, 17, 17, 17, 17, 17, 14 }, /* U */
        { 17, 17, 17, 17, 17, 10, 4 },  /* V */
        { 17, 17, 17, 21, 21, 27, 17 }, /* W */
        { 17, 10, 4, 4, 4, 10, 17 },    /* X */
        { 17, 10, 4, 4, 4, 4, 4 },      /* Y */
    };
    static const uint8_t letterIds[26] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
        13, 14, 15, 255, 16, 17, 18, 19, 20, 21, 22, 23, 255,
    };
    static const uint8_t colon[7] = { 0, 12, 12, 0, 12, 12, 0 };
    static const uint8_t dot[7] = { 0, 0, 0, 0, 0, 12, 12 };
    static const uint8_t dash[7] = { 0, 0, 0, 31, 0, 0, 0 };
    static const uint8_t slash[7] = { 1, 2, 2, 4, 8, 8, 16 };
    static const uint8_t percent[7] = { 19, 19, 2, 4, 8, 25, 25 };
    if (c == ':') return colon;
    if (c == '.') return dot;
    if (c == '-') return dash;
    if (c == '/') return slash;
    if (c == '%') return percent;
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') {
        uint8_t id = letterIds[c - 'A'];
        if (id != 255) return letters[id];
    }
    return NULL;
}

/* The single on-screen "FPS NN" overlay, drawn identically no matter which
 * path (GPU tile renderer or CPU scanline fallback) produced this frame, so
 * an intermittent fallback doesn't flip its size/position/font. `eyeXOffset`
 * is the per-eye horizontal shift (0 for mono / left, parallax for right).
 * Gated by the same MOSTRAR FPS config toggle as before. Must be called
 * inside an active C2D scene, after ConfigureAbgr/AtlasTextureEnv. */
void PlatformGpu3DS_DrawFpsOverlay(float eyeXOffset) {
    int pos = Port_Config_GetFpsPosition();
    if (pos == 0) return;
    /* Commit whatever the caller queued BEFORE touching the TEV. citro2d
     * batches C2D_DrawImage into its vertex buffer and only emits the draw
     * (with citro3d's *current* TEV state) at the next flush -- so writing
     * the TEV here while textured quads are still pending makes those quads
     * render with the solid-colour env instead of their texture, i.e. as
     * the untinted vertex colour (0), i.e. as nothing. That is what turned
     * the whole picture black in SCALED + STRETCH, the one display mode
     * where nothing else (pillarbox masks, bezel, HUD-outside pass) sits
     * between the tile batch and this overlay to flush it first. */
    C2D_Flush();
    PlatformGpu3DS_ResetSolidTexEnv();
    char label[20];
    double fps = Port_PPU_3DS_CurrentFps();
    unsigned rounded = fps > 0.0 ? (unsigned)(fps + 0.5) : 0u;
    if (rounded > 999u) rounded = 999u;
    snprintf(label, sizeof(label), "FPS %u", rounded);

    /* 1 = BOTTOM-LEFT, 2 = BOTTOM-RIGHT, 3 = TOP-LEFT, 4 = TOP-RIGHT */
    float boxX, boxY;
    switch (pos) {
        case 2: /* BOTTOM-RIGHT */
            boxX = 330.0f + eyeXOffset;
            boxY = 216.0f;
            break;
        case 3: /* TOP-LEFT */
            boxX = 5.0f + eyeXOffset;
            boxY = 6.0f;
            break;
        case 4: /* TOP-RIGHT */
            boxX = 330.0f + eyeXOffset;
            boxY = 6.0f;
            break;
        case 1: /* BOTTOM-LEFT (default) */
        default:
            boxX = 5.0f + eyeXOffset;
            boxY = 216.0f;
            break;
    }
    C2D_DrawRectSolid(boxX, boxY, 0.7f, 65.0f, 18.0f, C2D_Color32(0, 0, 0, 200));
    PlatformGpu3DS_DrawStatusText(boxX + 3.0f, boxY + 4.0f, 1.5f, label);
}

void PlatformGpu3DS_DrawStatusText(float x, float y, float scale, const char* text) {
    const uint32_t color = C2D_Color32(255, 255, 255, 255);
    for (; *text; ++text, x += 6.0f * scale) {
        const uint8_t* glyph = StatusGlyph(*text);
        if (!glyph) continue;
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5;) {
                if ((glyph[row] & (1u << (4 - col))) == 0) {
                    ++col;
                    continue;
                }
                int end = col + 1;
                while (end < 5 && (glyph[row] & (1u << (4 - end))) != 0) ++end;
                C2D_DrawRectSolid(x + col * scale, y + row * scale, 0.8f,
                                  (end - col) * scale, scale, color);
                col = end;
            }
        }
    }
}

static u32 TextureTransfer(void) {
    return GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) |
           GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
           GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
           GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
}

void PlatformGpu3DS_ConfigureAbgrTextureEnv(void) {
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_ALPHA, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    C3D_TexEnvColor(env, C2D_Color32(255, 0, 0, 255));

    env = C3D_GetTexEnv(1);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_B, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(env, C2D_Color32(0, 255, 0, 255));

    env = C3D_GetTexEnv(2);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_G, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(env, C2D_Color32(0, 0, 255, 255));
}

/* ConfigureAbgrTextureEnv() above wires TEV units 1 and 2 to sample
 * GPU_TEXTURE0 (the 3-stage byte-swizzle needed for the ABGR-composited
 * bottom-screen image). citro2d's own solid-color draws (C2D_DrawRectSolid,
 * used by the debug overlay and its font) only reprogram unit 0 when
 * switching from textured to solid mode -- it doesn't know our units 1/2
 * exist, so they stay wired to sample whatever texture unit 0 last pointed
 * at even though a solid draw binds no texture at all. Confirmed on real
 * hardware: a plain C2D_DrawRectSolid drawn right after
 * ConfigureAbgrTextureEnv() + C2D_DrawImage came out colorless/banded
 * instead of solid, exactly this leftover-TEV-state pattern (see
 * docs/3ds-port-gpu-renderer-status-2026-08-20.md section 19). Call this
 * after the textured draw and before any solid draw in the same scene to
 * put units 1/2 back to a harmless passthrough. */
void PlatformGpu3DS_ResetSolidTexEnv(void) {
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

    for (int i = 1; i < 6; ++i) {
        env = C3D_GetTexEnv(i);
        C3D_TexEnvInit(env);
    }
}

void PlatformGpu3DS_SetOld3DSProfile(bool on) { sOld3DSProfile = on; }

bool PlatformGpu3DS_Init(bool old3dsProfile) {
    memset(&sStats, 0, sizeof(sStats));
    LightLock_Init(&sGpuSubmitLock);
    sOld3DSProfile = old3dsProfile;
    sBottomTargetValid = false;
    sC2dFlushBase = NULL;
    sC2dFlushSize = 0;
    sTopUpload = (uint32_t*)linearMemAlign(TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t), 0x80);
    sTopRightUpload = (uint32_t*)linearMemAlign(TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t), 0x80);
    sBottomUploads[0] = (uint32_t*)linearMemAlign(512u * 256u * sizeof(uint32_t), 0x80);
    sBottomUploads[1] = (uint32_t*)linearMemAlign(512u * 256u * sizeof(uint32_t), 0x80);
    if (!sTopUpload || !sTopRightUpload || !sBottomUploads[0] || !sBottomUploads[1]) goto fail_linear;
    memset(sTopUpload, 0, TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t));
    memset(sTopRightUpload, 0, TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t));
    memset(sBottomUploads[0], 0, 512u * 256u * sizeof(uint32_t));
    memset(sBottomUploads[1], 0, 512u * 256u * sizeof(uint32_t));
    GSPGPU_FlushDataCache(sTopUpload, TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t));
    GSPGPU_FlushDataCache(sTopRightUpload, TOP_TEXTURE_WIDTH * TOP_TEXTURE_HEIGHT * sizeof(uint32_t));
    GSPGPU_FlushDataCache(sBottomUploads[0], 512u * 256u * sizeof(uint32_t));
    GSPGPU_FlushDataCache(sBottomUploads[1], 512u * 256u * sizeof(uint32_t));
    /* C3D_DEFAULT_CMDBUF_SIZE (256KB) is sized for typical homebrew scenes
     * (a handful of draws/frame). The GPU tile renderer draws 600-2500+
     * C2D_DrawImage calls per eye, and with the 3D slider on that doubles
     * (both eyes queue into the same one GPU command list before
     * C3D_FrameEnd) plus the bottom-screen composite and debug overlay on
     * top -- once *this* buffer (distinct from citro2d's own vertex buffer,
     * sized separately via C2D_Init below) fills up mid-frame, every GPU
     * command queued after that point is silently dropped for the rest of
     * the frame, with no error. That reads as exactly what turned up
     * testing with the 3D slider on: parts of the right eye missing
     * (queued after the left eye already used a large chunk of the buffer)
     * and the bottom-screen overlay (queued last, after both eyes) entirely
     * absent -- the same "silently drops everything past a fixed capacity"
     * failure mode as the citro2d vertex-buffer exhaustion bug fixed
     * earlier (see the C2D_Init comment below), just a different buffer.
     * 4x default gives real headroom for the worst case (2 eyes x
     * MAX_DRAW_ITEMS-ish); PORT_GPU_RENDERER_DIAG_LOG logs
     * C3D_GetCmdBufUsage() each frame to confirm this empirically instead
     * of guessing at a size. */
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4)) goto fail_linear;
    /* 128 was sized for this module's own original use case (a couple of
     * big background quads plus the small FPS-text overlay, well under
     * 100 quads/frame). port_gpu_renderer.c's tile/sprite compositor draws
     * 600-1200+ C2D_DrawImage calls in a single scene for a full-screen BG
     * -- once that exceeds citro2d's internal vertex buffer capacity here,
     * everything past the limit silently doesn't render, which reads as
     * "only the first few rows/top of the screen show, the rest is black"
     * regardless of what the scene actually contains. Matches exactly what
     * showed up testing the GPU renderer. 4096 covers the GPU renderer's
     * documented worst case (port_gpu_renderer.c's MAX_DRAW_ITEMS) with
     * headroom. */
    /* Bumped from 4096: that covered one eye's worst case (MAX_DRAW_ITEMS)
     * alone, but with the 3D slider on, both eyes' draws land in this same
     * buffer within one C3D frame before it's fully consumed -- confirmed
     * via a real-hardware screenshot (KEY_X dump, PlatformGpu3DS_DumpScreens)
     * showing the topmost BG layer (menu box, HUD) completely absent from
     * the right eye's target specifically while the same item COUNT was
     * submitted for both eyes (see the EYE0/EYE1 diag log in
     * port_gpu_renderer.c) -- i.e. late-in-the-frame content silently not
     * rendering, the same failure signature as the original citro2d
     * buffer-exhaustion bug (see the comment below), just triggered by
     * cumulative draws across both eyes instead of within a single one. 3x
     * covers two full eyes' worth of MAX_DRAW_ITEMS with headroom. */
    if (!C2D_Init(4096 * 3)) {
        C3D_Fini();
        goto fail_linear;
    }
    C2D_Prepare();
    C3D_BufInfo* c2dBuffers = C3D_GetBufInfo();
    if (c2dBuffers && c2dBuffers->bufCount > 0) {
        const u32 heapPhysical = osConvertVirtToPhys((void*)__ctru_linear_heap);
        const u32 vertexPhysical = c2dBuffers->base_paddr + c2dBuffers->buffers[0].offset;
        const uintptr_t heapStart = (uintptr_t)__ctru_linear_heap;
        const uintptr_t heapEnd = heapStart + __ctru_linear_heap_size;
        const uintptr_t vertexAddress = heapStart + (u32)(vertexPhysical - heapPhysical);
        const uintptr_t flushStart = vertexAddress & ~(uintptr_t)0x7Fu;
        uintptr_t flushEnd = flushStart + 64u * 1024u;
        if (flushEnd > heapEnd) flushEnd = heapEnd;
        if (flushStart >= heapStart && flushStart < flushEnd) {
            sC2dFlushBase = (void*)flushStart;
            sC2dFlushSize = flushEnd - flushStart;
        }
    }
    sStats.linearHeapBytes = __ctru_linear_heap_size;
    sStats.c2dFlushBytes = (uint32_t)sC2dFlushSize;
    sStats.c2dFlushAddress = (uintptr_t)sC2dFlushBase;
    sStats.topUploadAddress = (uintptr_t)sTopUpload;
    sStats.bottomUploadAddress[0] = (uintptr_t)sBottomUploads[0];
    sStats.bottomUploadAddress[1] = (uintptr_t)sBottomUploads[1];
    /* sTopRightUpload is allocated with the other upload buffers above (and
     * zeroed + flushed there). A second linearMemAlign used to sit here and
     * overwrite the pointer: 512KB of linear heap leaked for the life of the
     * process, and the buffer actually in use was the uninitialised one. */
    /* The CPU renderer's frame textures live in linear memory, not VRAM: that
     * renderer is only the fallback for the few frames the GPU one cannot
     * draw, so sampling from FCRAM costs nothing that matters, while the 1MB
     * they would take is what the GPU renderer's WIDE haze targets need
     * (port_gpu_renderer.c). The display transfer writes either just fine. */
    if (!C3D_TexInit(&sTopTexture, TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT, GPU_RGBA8)) goto fail;
    if (!C3D_TexInit(&sTopRightTexture, TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT, GPU_RGBA8)) goto fail_top_texture;
    if (!C3D_TexInitVRAM(&sBottomTexture, 512, 256, GPU_RGBA8)) goto fail_top_right_texture;
    C3D_TexSetFilter(&sTopTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetFilter(&sTopRightTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetFilter(&sBottomTexture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sTopTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexSetWrap(&sTopRightTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexSetWrap(&sBottomTexture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    gfxSet3D(true);
    sTopTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    sTopRightTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    sBottomTarget = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!sTopTarget || !sBottomTarget) goto fail_targets;
    const u32 output = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                       GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                       GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
                       GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_RenderTargetSetOutput(sTopTarget, GFX_TOP, GFX_LEFT, output);
    if (sTopRightTarget) C3D_RenderTargetSetOutput(sTopRightTarget, GFX_TOP, GFX_RIGHT, output);
    C3D_RenderTargetSetOutput(sBottomTarget, GFX_BOTTOM, GFX_LEFT, output);
#ifdef PORT_DEBUG_TOOLS_ACTIVE
    {
        /* Diagnostic (docs/.../3ds-port-gpu-renderer-status-2026-08-20.md
         * section 20): dump every field of both targets' C3D_FrameBuf/
         * C3D_RenderTarget at creation, side by side, looking for any
         * register-level difference between sTopTarget (never shows the
         * duplication bug) and sBottomTarget (always does). Confirmed on
         * hardware (2026-08-21): both structurally identical aside from
         * expected dims/addresses/screen id -- ruled out a config
         * difference as the cause. */
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "TARGETCMP top: w=%u h=%u colorBuf=%p depthBuf=%p colorFmt=%d linked=%d screen=%d side=%d xfer=%08lx",
                 sTopTarget->frameBuf.width, sTopTarget->frameBuf.height, sTopTarget->frameBuf.colorBuf,
                 sTopTarget->frameBuf.depthBuf, (int)sTopTarget->frameBuf.colorFmt, (int)sTopTarget->linked,
                 (int)sTopTarget->screen, (int)sTopTarget->side, (unsigned long)sTopTarget->transferFlags);
        extern void Port_DebugLog_Gpu(const char* msg);
        Port_DebugLog_Gpu(msg);
        snprintf(msg, sizeof(msg),
                 "TARGETCMP bot: w=%u h=%u colorBuf=%p depthBuf=%p colorFmt=%d linked=%d screen=%d side=%d xfer=%08lx",
                 sBottomTarget->frameBuf.width, sBottomTarget->frameBuf.height, sBottomTarget->frameBuf.colorBuf,
                 sBottomTarget->frameBuf.depthBuf, (int)sBottomTarget->frameBuf.colorFmt, (int)sBottomTarget->linked,
                 (int)sBottomTarget->screen, (int)sBottomTarget->side, (unsigned long)sBottomTarget->transferFlags);
        Port_DebugLog_Gpu(msg);
    }
#endif
    PortGbaScreenFx_Init(); /* non-fatal: only disables the ghost effect on failure */
    PortGbaBezel_Init();    /* non-fatal: only disables bezel on failure */
    sReady = true;
    return true;

fail_targets:
    if (sBottomTarget) C3D_RenderTargetDelete(sBottomTarget);
    if (sTopRightTarget) C3D_RenderTargetDelete(sTopRightTarget);
    if (sTopTarget) C3D_RenderTargetDelete(sTopTarget);
    C3D_TexDelete(&sBottomTexture);
fail_top_right_texture:
    C3D_TexDelete(&sTopRightTexture);
fail_top_texture:
    C3D_TexDelete(&sTopTexture);
fail:
    C2D_Fini();
    C3D_Fini();
fail_linear:
    if (sBottomUploads[1]) linearFree(sBottomUploads[1]);
    if (sBottomUploads[0]) linearFree(sBottomUploads[0]);
    if (sTopRightUpload) linearFree(sTopRightUpload);
    if (sTopUpload) linearFree(sTopUpload);
    sBottomUploads[0] = NULL;
    sBottomUploads[1] = NULL;
    sTopRightUpload = NULL;
    sTopUpload = NULL;
    return false;
}

float PlatformGpu3DS_Get3DSlider(void) { return osGet3DSliderState(); }

/* Left top target only, to a caller-chosen path. Same mechanism and byte
 * layout as the screen dump's own mzm-dump-NN-left.rgb (240x400 portrait,
 * BGR -- see docs/3ds-debug-tools.md). */
static void DumpOneTarget(C3D_RenderTarget* target, const char* path); /* defined below */

/* Stop anything else drawing into the targets, then let what is already
 * queued retire, so a dump reads a finished frame rather than one being
 * overwritten underneath it. The present thread (Port_PPU_GpuPresentPump)
 * submits the game's own frames into these very targets, so a dump that
 * does not hold the lock is a genuine race regardless of anything else.
 *
 * Honest scope: this closes that race, and it does NOT fix the dumps that
 * come back torn into vertical bands under Azahar -- those are unchanged
 * with the lock held, and they reproduce with the CPU scanline renderer as
 * readily as with the tile one, so whatever is left is neither renderer's
 * doing. See docs/3ds-renderer-perf-plan.md.
 *
 * VBlank and not gspWaitForP3D/gspWaitForPPF: those look like the precise
 * primitives, but each waits on an event that has usually already been
 * consumed, so the second target of a three-target dump blocks forever
 * (observed as a dump set holding only -left.rgb). VBlank always comes.
 *
 * LightLock is not recursive, so callers must NOT already hold it. */
static void DumpQuiesceBegin(void) {
    PlatformGpu3DS_SubmitLock_Acquire();
    gspWaitForVBlank();
    gspWaitForVBlank();
}

static void DumpQuiesceEnd(void) { PlatformGpu3DS_SubmitLock_Release(); }

uint32_t* PlatformGpu3DS_TopBuffer(void) { return sTopUpload; }
uint32_t* PlatformGpu3DS_TopRightBuffer(void) { return sTopRightUpload; }
uint32_t* PlatformGpu3DS_BottomBuffer(unsigned index) {
    return index < 2 ? sBottomUploads[index] : NULL;
}

static void DrawTopImageStereo(const uint32_t* leftPixels, const uint32_t* rightPixels, unsigned width) {
    if (width < 240u) width = 240u;
    if (width > 266u) width = 266u;
    sTopPresentWidth = width;

    GSPGPU_FlushDataCache(leftPixels, TOP_TEXTURE_WIDTH * 160u * sizeof(uint32_t));
    const unsigned sourceHeight = 160u;
    C3D_SyncDisplayTransfer((u32*)leftPixels, GX_BUFFER_DIM(TOP_TEXTURE_WIDTH, sourceHeight),
                            (u32*)sTopTexture.data, GX_BUFFER_DIM(TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT),
                            TextureTransfer());

    const uint32_t* rightSrc = (rightPixels && sTopRightTexture.data) ? rightPixels : leftPixels;
    C3D_Tex* rightTex = (rightPixels && sTopRightTexture.data) ? &sTopRightTexture : &sTopTexture;
    if (rightPixels && rightPixels != leftPixels && sTopRightTexture.data) {
        GSPGPU_FlushDataCache(rightPixels, TOP_TEXTURE_WIDTH * 160u * sizeof(uint32_t));
        C3D_SyncDisplayTransfer((u32*)rightPixels, GX_BUFFER_DIM(TOP_TEXTURE_WIDTH, sourceHeight),
                                (u32*)sTopRightTexture.data, GX_BUFFER_DIM(TOP_TEXTURE_WIDTH, TOP_TEXTURE_HEIGHT),
                                TextureTransfer());
    }

    /* The span is the full `width` x 160 texels -- correct, and what makes a
     * scaled present sample the source uniformly -- but pinned at 0 it puts
     * a third of the samples EXACTLY on a texel boundary at 3:2 (120 of 360
     * columns), where which side they land is up to the hardware's
     * rounding. On a real PICA200 they do not all land the same way, and
     * the result is a picture whose straight lines are not straight: the
     * "dirty lines" the CPU renderer has always had when scaled.
     *
     * Shifting the span by an eighth of a source pixel breaks those ties
     * consistently and costs nothing else -- the outermost samples move by
     * an eighth of a pixel and stay far inside the image. The same shift as
     * the GPU tile renderer's atlas UVs (ATLAS_UV_TIE_SHIFT), on purpose:
     * with both shifted the same way the two renderers agree pixel for
     * pixel at 3:2, which is the whole point -- one is the oracle for the
     * other. */
    const float uvTieShift = 0.125f;
    sTopSubtexture = (Tex3DS_SubTexture){
        .width = (u16)width, .height = 160,
        .left = uvTieShift / TOP_TEXTURE_WIDTH,
        .top = 1.0f - uvTieShift / TOP_TEXTURE_HEIGHT,
        .right = ((float)width + uvTieShift) / TOP_TEXTURE_WIDTH,
        .bottom = 1.0f - (160.0f + uvTieShift) / TOP_TEXTURE_HEIGHT,
    };
    const C2D_Image imageLeft = { .tex = &sTopTexture, .subtex = &sTopSubtexture };
    const C2D_Image imageRight = { .tex = rightTex, .subtex = &sTopSubtexture };
    const int style = Port_Config_Get3DSDisplayStyle();
    PortWide_SetFrameDrawn(false); /* this path only ever draws the plain frame */
    C3D_TexSetFilter(&sTopTexture, style == TOP_DISPLAY_BLUR ? GPU_LINEAR : GPU_NEAREST,
                     style == TOP_DISPLAY_BLUR ? GPU_LINEAR : GPU_NEAREST);
    if (sTopRightTexture.data) {
        C3D_TexSetFilter(&sTopRightTexture, style == TOP_DISPLAY_BLUR ? GPU_LINEAR : GPU_NEAREST,
                         style == TOP_DISPLAY_BLUR ? GPU_LINEAR : GPU_NEAREST);
    }

    float drawW;
    float drawH;
    if (style == TOP_DISPLAY_PIXEL_PERFECT) {
        drawW = (float)width;
        drawH = 160.0f;
    } else {
        drawH = 240.0f;
        /* Only STRETCH fills the screen width; ORIGINAL, WIDE (whose extra
         * world only the GPU renderer draws) and any stale out-of-range value
         * pillarbox to 360. Kept identical to
         * PlatformGpu3DS_GetTopImageRect and to port_gpu_renderer.c's
         * screenBaseX/scaleX: a rect that disagrees with what was actually
         * drawn misplaces every screen-space effect built on it. */
        drawW = (Port_Config_Get3DSAspectRatio() == TOP_ASPECT_STRETCH) ? 400.0f : 360.0f;
    }
    const C2D_DrawParams params = {
        .pos = { .x = (400.0f - drawW) * 0.5f, .y = (240.0f - drawH) * 0.5f,
                 .w = drawW, .h = drawH },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };

    C2D_TargetClear(sTopTarget, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(sTopTarget);
    C2D_DrawImage(imageLeft, &params, NULL);
    /* Set the channel-swizzle env and commit the picture quad right here.
     * The env has to be written after C2D_DrawImage (citro2d re-inits TEV 0
     * on the first draw of a scene) but before the flush that actually
     * emits it -- and every later pass (bezel, FPS overlay) starts by
     * resetting the TEV, so leaving the quad pending across them drew it
     * with the solid-colour env: a black top screen. */
    PlatformGpu3DS_ConfigureAbgrTextureEnv();
    C2D_Flush();
    if (PortGbaBezel_Active()) {
        PortGbaBezel_Draw(sTopTarget);
    }
    PlatformGpu3DS_DrawFpsOverlay(0.0f);

    if (sTopRightTarget) {
        C2D_TargetClear(sTopRightTarget, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(sTopRightTarget);
        C2D_DrawImage(imageRight, &params, NULL);
        PlatformGpu3DS_ConfigureAbgrTextureEnv(); /* see the left eye above */
        C2D_Flush();
        if (PortGbaBezel_Active()) {
            PortGbaBezel_Draw(sTopRightTarget);
        }
        PlatformGpu3DS_DrawFpsOverlay(0.0f);
    }
}

void PlatformGpu3DS_BeginTop(const uint32_t* pixels, unsigned width) {
    PlatformGpu3DS_BeginTopStereo(pixels, NULL, width);
}

void PlatformGpu3DS_BeginTopStereo(const uint32_t* leftPixels, const uint32_t* rightPixels, unsigned width) {
    if (!sReady || !leftPixels) return;
    if (!C3D_FrameBegin(0)) {
        ++sStats.frameBeginFailures;
        return;
    }
    sFrameActive = true;
    DrawTopImageStereo(leftPixels, rightPixels, width);
    ++sStats.topTransfers;
}

/* CPU time the last C3D_FrameBegin spent waiting for the GPU to finish the
 * previous frame -- the part of a frame where the CPU does nothing. */
static float sLastGpuWaitMs;
float PlatformGpu3DS_LastGpuWaitMs(void) { return sLastGpuWaitMs; }

/* Accumulated per phase over the frame in flight; handed to the perf sample
 * and cleared at the end of PlatformGpu3DS_EndBottom. */
static u64 sPhaseTicks[PERF_PHASE_COUNT];
void PlatformGpu3DS_PerfPhaseAdd(PerfPhase phase, unsigned long long ticks) {
    if ((unsigned)phase < PERF_PHASE_COUNT) sPhaseTicks[phase] += ticks;
}
static uint32_t sPerfCounts[PERF_COUNT_COUNT];
void PlatformGpu3DS_PerfCountAdd(PerfCounter counter, unsigned count) {
    if ((unsigned)counter < PERF_COUNT_COUNT) sPerfCounts[counter] += count;
}

bool PlatformGpu3DS_BeginTopSceneGpu(void) {
    if (!sReady) return false;
    /* Already begun this frame (the renderer can begin early when its
     * collection has to wait for an idle GPU, see port_ppu_mzm.c). */
    if (sFrameActive) return true;
    const u64 waitStart = svcGetSystemTick();
    /* Issue #17: tested C3D_FRAME_SYNCDRAW here (forces the CPU to wait for
     * the GPU to finish the previous frame before this frame's CPU-side
     * atlas decode writes start) as a test for a CPU/GPU frame-overlap race
     * on the shared, non-double-buffered atlas texture. Confirmed on real
     * hardware (2026-08-26) NOT to fix the death-scene corruption -- still
     * broken, just slower (dropped FPS game-wide). Reverted to plain
     * C3D_FrameBegin(0). See docs/3ds-issue17-session-2026-08-25.md's final
     * update -- this rules out frame-overlap as the cause. */
    if (!C3D_FrameBegin(0)) {
        ++sStats.frameBeginFailures;
        return false;
    }
    sLastGpuWaitMs = (float)((double)(svcGetSystemTick() - waitStart) / CPU_TICKS_PER_MSEC);
    sFrameActive = true;
    ++sStats.topTransfers;
    return true;
}

C3D_RenderTarget* PlatformGpu3DS_GetTopLeftTarget(void) { return sTopTarget; }
C3D_RenderTarget* PlatformGpu3DS_GetTopRightTarget(void) { return sTopRightTarget; }

/* Where the emulated GBA image actually lands on the 400x240 top screen for
 * the current display style / aspect -- the same rectangle DrawTopImageStereo
 * and the GPU tile renderer draw into. Screen-space effects (port_gba_screen_fx)
 * need this so they only touch the picture, not the letterbox. Native GBA
 * width is fixed at 240 (TOP_NATIVE_W), matching both present paths. */
void PlatformGpu3DS_GetTopImageRect(int* outX, int* outY, int* outW, int* outH) {
    int w, h;
    if (PortWide_FrameDrawn()) {
        /* WIDE: the picture is the whole screen. */
        w = 400; h = 240;
    } else if (Port_Config_Get3DSDisplayStyle() == TOP_DISPLAY_PIXEL_PERFECT) {
        w = 240; h = 160;
    } else {
        h = 240;
        /* See DrawTopImageStereo: STRETCH is the only full-width aspect. */
        w = (Port_Config_Get3DSAspectRatio() == TOP_ASPECT_STRETCH) ? 400 : 360;
    }
    if (outX) *outX = (400 - w) / 2;
    if (outY) *outY = (240 - h) / 2;
    if (outW) *outW = w;
    if (outH) *outH = h;
}

/* Defined with the perf recorder further down; called from EndBottom once
 * the frame's real cost is known. */
static void PerfBackfillPresentedFrame(void);

bool PlatformGpu3DS_EndBottom(const uint32_t* pixels, bool changed) {
    if (!sFrameActive || !pixels) return false;
#ifdef PORT_DEBUG_TOOLS_ACTIVE
    {
        /* Diagnostic (docs/.../3ds-port-gpu-renderer-status-2026-08-20.md
         * section 20 pendiente item 4): log every real EndBottom call
         * unthrottled for the first 120 (~2s at 60fps) so we can see
         * whether the duplication is present from frame 1 or appears
         * later, then throttle to keep the log file from growing forever
         * during a real play session. Confirmed on hardware (2026-08-21):
         * present from frame 1, changed=1 and the "real draw" branch taken
         * on every single call (New3DS never hits EndBottom's own/else
         * skip branch) -- ruled out reentrancy/skip-branch theories. */
        static unsigned sEndBottomCallCount;
        static u64 sFirstCallTick;
        const u64 nowTick = svcGetSystemTick();
        if (sFirstCallTick == 0) sFirstCallTick = nowTick;
        const unsigned n = ++sEndBottomCallCount;
        if (n <= 120 || (n % 60u) == 0u) {
            char msg[128];
            const double msSinceFirst = (double)(nowTick - sFirstCallTick) / (double)CPU_TICKS_PER_MSEC;
            snprintf(msg, sizeof(msg), "ENDBOTTOM n=%u changed=%d valid=%d old3ds=%d t=%.0fms",
                     n, (int)changed, (int)sBottomTargetValid, (int)sOld3DSProfile, msSinceFirst);
            extern void Port_DebugLog_Gpu(const char* msg);
            Port_DebugLog_Gpu(msg);
        }
    }
#endif
    /* `changed` is always true from the callers and is no longer the redraw
     * signal: the bottom UI owns its own cadence now. It advances per-frame
     * state every frame, but the actual rasterise (several thousand
     * immediate-mode quads for a text/map view -- enough to tip the game
     * frame past the vsync budget) only runs on a ~20Hz throttle, or
     * immediately after any interaction. The persistent render target is
     * scanned out unchanged on the frames in between. */
    (void)changed;

    /* GBA-look effects: a self-contained pass over the finished top target(s),
     * here because both present paths (CPU scanline via DrawTopImageStereo,
     * PICA tiler via Port_GpuRenderer_RenderFrame) converge on EndBottom with
     * the frame still open and the bottom scene not yet bound. */
    const u64 fxStart = svcGetSystemTick();
    if (PortGbaScreenFx_Active()) {
        C3D_RenderTarget* fxRight =
            (PlatformGpu3DS_Get3DSlider() > 0.01f) ? sTopRightTarget : NULL;
        PortGbaScreenFx_PostProcessTop(sTopTarget, fxRight);
    }

    extern void Port_BottomUI_FrameTick(void);
    extern bool Port_BottomUI_WantsRedraw(void);
    const u64 bottomStart = svcGetSystemTick();
    PlatformGpu3DS_PerfPhaseAdd(PERF_PHASE_FRAME_END, bottomStart - fxStart);
    Port_BottomUI_FrameTick();
    const bool redrawBottom = Port_BottomUI_WantsRedraw();

    if (redrawBottom || !sBottomTargetValid) {
        C2D_TargetClear(sBottomTarget, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(sBottomTarget);
        PlatformGpu3DS_ResetSolidTexEnv();
        extern void Port_BottomUI_Render(void);
        Port_BottomUI_Render();
        sBottomTargetValid = true;
        ++sStats.bottomTargetDraws;
    } else {
        ++sStats.bottomTargetReuseSkips;
    }
    C2D_Flush();
    if (sC2dFlushBase && sC2dFlushSize) {
        GSPGPU_FlushDataCache(sC2dFlushBase, sC2dFlushSize);
        sStats.boundedFlushBytes += sC2dFlushSize;
    }
#ifdef PORT_DEBUG_TOOLS_ACTIVE
    {
        static unsigned sCmdBufLogCounter;
        if ((sCmdBufLogCounter++ % 30u) == 0u) {
            char msg[64];
            snprintf(msg, sizeof(msg), "CMDBUF usage=%.1f%%", (double)(C3D_GetCmdBufUsage() * 100.0f));
            /* GPU log stream: buffered (per-frame, throttled -- see
             * port_debug_log.h) and only when LOG mode is ALL or GPU. */
            extern void Port_DebugLog_Gpu(const char* msg);
            Port_DebugLog_Gpu(msg);
        }
    }
#endif
    const u64 frameEndStart = svcGetSystemTick();
    PlatformGpu3DS_PerfPhaseAdd(PERF_PHASE_BOTTOM_UI, frameEndStart - bottomStart);
    C3D_FrameEnd(0);
    const u64 syncStart = svcGetSystemTick();
    PlatformGpu3DS_PerfPhaseAdd(PERF_PHASE_FRAME_END, syncStart - frameEndStart);
    /* Cap presentation to the LCD refresh rate. Without this, nothing paces
     * the main loop to VBlank and it free-runs as fast as the CPU/GPU allow
     * (60-120+ FPS depending on scene load), speeding up game logic and
     * audio with it. Regressed by 1dae106c, which dropped this call thinking
     * it was redundant with C3D_FrameEnd's flags. */
    C3D_FrameSync();
    PlatformGpu3DS_PerfPhaseAdd(PERF_PHASE_VSYNC, svcGetSystemTick() - syncStart);
    ++sStats.frames;
    sStats.drawingTime = C3D_GetDrawingTime();
    sStats.processingTime = C3D_GetProcessingTime();
    PerfBackfillPresentedFrame();
    memset(sPhaseTicks, 0, sizeof(sPhaseTicks));
    memset(sPerfCounts, 0, sizeof(sPerfCounts));
    sFrameActive = false;
    return true;

}

void PlatformGpu3DS_ShowDumpSavedOverlay(void) {
    if (!sReady || !sTopUpload || !sBottomUploads[0] || !C3D_FrameBegin(0)) return;
    sFrameActive = true;
    DrawTopImage(sTopUpload, sTopPresentWidth);
    C2D_DrawRectSolid(132.0f, 12.0f, 0.7f, 136.0f, 24.0f, C2D_Color32(0, 0, 0, 220));
    PlatformGpu3DS_DrawStatusText(141.0f, 17.0f, 2.0f, "DUMP SAVED");

    sBottomSubtexture = (Tex3DS_SubTexture){
        .width = 320, .height = 240, .left = 0.0f, .top = 1.0f,
        .right = 320.0f / 512.0f, .bottom = 1.0f - 240.0f / 256.0f,
    };
    const C2D_Image bottomImage = { .tex = &sBottomTexture, .subtex = &sBottomSubtexture };
    const C2D_DrawParams bottomParams = {
        .pos = { .x = 0.0f, .y = 0.0f, .w = 320.0f, .h = 240.0f },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };
    C2D_SceneBegin(sBottomTarget);
    C2D_DrawImage(bottomImage, &bottomParams, NULL);
    PlatformGpu3DS_ConfigureAbgrTextureEnv();
    C2D_Flush();
    if (sC2dFlushBase && sC2dFlushSize) GSPGPU_FlushDataCache(sC2dFlushBase, sC2dFlushSize);
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    sFrameActive = false;
    gspWaitForEvent(GSPGPU_EVENT_VBlank0, false);
    svcSleepThread(600000000LL);
}

void PlatformGpu3DS_GetStats(PlatformGpu3DSStats* stats) {
    if (stats) *stats = sStats;
}

void PlatformGpu3DS_InvalidateBottomTarget(void) {
    /* HOME and lid sleep may invalidate or rotate the physical framebuffer.
     * Force one opaque redraw after APT resumes before Old 3DS starts reusing
     * the unchanged target again. */
    sBottomTargetValid = false;
}

void PlatformGpu3DS_Shutdown(void) {
    if (!sReady) return;
    if (sFrameActive) {
        C2D_Flush();
        if (sC2dFlushBase && sC2dFlushSize) GSPGPU_FlushDataCache(sC2dFlushBase, sC2dFlushSize);
        C3D_FrameEnd(GX_CMDLIST_FLUSH);
    }
    if (!aptShouldClose()) C3D_FrameSync();
    PortGbaBezel_Shutdown();
    PortGbaScreenFx_Shutdown();
    C3D_RenderTargetDelete(sBottomTarget);
    C3D_RenderTargetDelete(sTopTarget);
    C3D_TexDelete(&sBottomTexture);
    C3D_TexDelete(&sTopTexture);
    C2D_Fini();
    C3D_Fini();
    linearFree(sBottomUploads[1]);
    linearFree(sBottomUploads[0]);
    linearFree(sTopUpload);
    sBottomUploads[0] = NULL;
    sBottomUploads[1] = NULL;
    sTopUpload = NULL;
    sC2dFlushBase = NULL;
    sC2dFlushSize = 0;
    sFrameActive = false;
    sReady = false;
    sOld3DSProfile = false;
    sBottomTargetValid = false;
}

/* Diagnostic screenshot: reads back the actual GPU-rendered content of all
 * three render targets (left eye, right eye if stereo is active, bottom)
 * straight from VRAM and writes each as a headerless raw RGB8 file to the
 * SD card -- unlike any CPU-side source buffer, this shows exactly what the
 * PICA200 actually produced, which is the only way to see what's really in
 * the right-eye target for the "menu/floor invisible in right eye with 3D
 * on" bug (docs/3ds-port-gpu-renderer-status-2026-08-20.md is being updated
 * with the investigation). Files are raw top-to-bottom RGB8, no header, at
 * each target's native (portrait, unrotated) dimensions -- rotate 90
 * degrees when viewing. Triggered by KEY_X (see Platform3DS_PollKeysIntoGba
 * in platform_3ds_minimal.c). Not gated behind any diag flag since it does
 * nothing unless the player actually presses X. */
static void DumpOneTarget(C3D_RenderTarget* target, const char* path) {
    if (!target || !target->frameBuf.colorBuf) return;
    const u16 w = target->frameBuf.width;
    const u16 h = target->frameBuf.height;
    const size_t sz = (size_t)w * h * 3u;
    u32* buf = (u32*)linearAlloc(sz);
    if (!buf) return;
    const u32 flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                       GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
                       GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_SyncDisplayTransfer((u32*)target->frameBuf.colorBuf, GX_BUFFER_DIM(w, h), buf, GX_BUFFER_DIM(w, h), flags);
    GSPGPU_FlushDataCache(buf, sz);
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(buf, 1, sz, f);
        fclose(f);
    }
    linearFree(buf);
}

/* One render target as a 24-bit BMP (debug_link_3ds.c, SITE SHOT): the target is read back as
 * the dump does (portrait, B G R, 240 pixels per column, bottom of the screen first), turned
 * into a picture right side up, built in RAM and written at once. */
static bool ShotOneTarget(C3D_RenderTarget* target, const char* path) {
    if (!target || !target->frameBuf.colorBuf) return false;
    const u16 w = target->frameBuf.width;  /* 240: the screen's height */
    const u16 h = target->frameBuf.height; /* 400 or 320: the screen's width */
    const size_t sz = (size_t)w * h * 3u;
    u8* src = (u8*)linearAlloc(sz);
    if (!src) return false;
    const u32 flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                       GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
                       GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    C3D_SyncDisplayTransfer((u32*)target->frameBuf.colorBuf, GX_BUFFER_DIM(w, h), (u32*)src, GX_BUFFER_DIM(w, h), flags);
    GSPGPU_FlushDataCache(src, sz);
    const unsigned width = h, height = w;
    const unsigned stride = (width * 3u + 3u) & ~3u;
    const size_t total = 54u + (size_t)stride * height;
    u8* bmp = (u8*)calloc(1, total);
    bool ok = false;
    if (bmp) {
        const u32 fsize = (u32)total, offset = 54u, info = 40u, planesBits = 1u | (24u << 16), image = (u32)(stride * height);
        const s32 sw = (s32)width, sh = (s32)height;
        bmp[0] = 'B';
        bmp[1] = 'M';
        memcpy(bmp + 2, &fsize, 4);
        memcpy(bmp + 10, &offset, 4);
        memcpy(bmp + 14, &info, 4);
        memcpy(bmp + 18, &sw, 4);
        memcpy(bmp + 22, &sh, 4);
        memcpy(bmp + 26, &planesBits, 4);
        memcpy(bmp + 34, &image, 4);
        for (unsigned y = 0; y < height; ++y) {
            u8* row = bmp + 54u + (size_t)(height - 1u - y) * stride; /* bottom-up */
            for (unsigned x = 0; x < width; ++x) {
                const u8* p = src + ((size_t)x * height + (height - 1u - y)) * 3u;
                row[x * 3 + 0] = p[0];
                row[x * 3 + 1] = p[1];
                row[x * 3 + 2] = p[2];
            }
        }
        FILE* f = fopen(path, "wb");
        ok = f && fwrite(bmp, 1, total, f) == total;
        if (f && fclose(f) != 0) ok = false;
        free(bmp);
    }
    linearFree(src);
    return ok;
}

bool PlatformGpu3DS_SaveShots(const char* topPath, const char* bottomPath) {
    if (!sReady) return false;
    DumpQuiesceBegin();
    const bool top = ShotOneTarget(sTopTarget, topPath);
    const bool bottom = ShotOneTarget(sBottomTarget, bottomPath);
    DumpQuiesceEnd();
    return top && bottom;
}

static void WriteBlob(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, size, f);
    fclose(f);
}

/* Scene recorder: the debug tools menu (port_bottom_ui_3ds.c) toggles this
 * on/off. Unlike PlatformGpu3DS_DumpScreens' one-shot dump, this samples the
 * emulated GBA state (VRAM/OAM/palettes/IO + a bit of Samus state) at the
 * rate the selected kRecPresets entry picks, appending each sample either
 * straight to a growing SD file or into a RAM buffer flushed on stop (see
 * kRecPresets) -- built for tracking down issue #17, where a single L+R+X
 * press couldn't catch the exact moment the death animation actually breaks
 * (as opposed to the correct green-flash frames right after it starts).
 * Deliberately skips the RGB screenshots DumpScreens takes (each is ~800KB
 * and requires a GPU display-transfer sync): at 15 samples/sec those would
 * either stall the frame pump or need buffering far bigger than this format,
 * and the VRAM/OAM/palette state alone is enough to reconstruct the visual
 * frame-by-frame offline (already proven doing that for the L+R+X dumps).
 * One sample is ~101KB, so a multi-second recording (say 5s at 15Hz = 75
 * samples, ~7.5MB) is trivial for the SD card; kRecordMaxSamples caps it so
 * forgetting to press the combo again doesn't fill the card.
 *
 * Companion screenshots (issue #17 follow-up): every
 * kRecordScreenshotEverySamples-th sample ALSO gets a real left-eye
 * DumpOneTarget() screenshot (same ~800KB/GPU-sync cost DumpScreens' single
 * shot has), written to its own indexed file rather than folded into
 * mzm-rec.bin's fixed-size records. At 1-in-4 (~4/sec) this is affordable
 * for the few seconds #17's death sequence lasts, unlike doing it every
 * sample (see the no-screenshots reasoning above). This is the only way to
 * tell apart "the emulated state was already wrong" (visible in every
 * sample via the existing VRAM/OAM/palette reconstruction) from "the state
 * was fine but the GPU renderer drew it wrong" (only provable by comparing
 * against what was ACTUALLY on screen at that exact sample). */
static bool sRecording;
static FILE* sRecFile;
static unsigned sRecFrameCounter;
static unsigned sRecSampleCount;
/* Wall-clock ticks of the emulated frame that just ended, measured between
 * consecutive PlatformGpu3DS_RecordTick calls (which Port_Bios_Halt calls
 * exactly once per emulated GBA frame). Recorded per sample so a recording
 * can show WHEN frames overran the 16.7ms budget (issue #20's Skree-explosion
 * hitch) instead of only what was on screen. */
static u64 sRecLastTick;
static u64 sRecLastFrameTicks;

extern u64 Platform3DS_SystemTick(void);
extern u64 Platform3DS_TicksPerSecond(void);
static const unsigned kRecordMaxSamples = 450; /* streaming-mode cap (~30s at 15Hz) */
/* Streaming mode only. Each shot is a GPU quiesce (2 VBlank waits) + a
 * ~280KB SD write -- at every 4 samples that alone can halve the frame rate
 * on a slow card. One per ~2 s at 15Hz is plenty to eyeball a capture. */
static const unsigned kRecordScreenshotEverySamples = 30;
/* Upper bound of one sample: header + IO + BgPltt + ObjPltt + OAM + VRAM +
 * the clip block (<=256). RAM slot stride in RING mode. */
static const size_t kRecordSampleMaxBytes =
    64 + 1024 + 512 + 512 + 1024 + 0x18000 + 256;

/* Runtime-selectable recorder presets, cycled from the bottom-screen debug
 * button (PlatformGpu3DS_CycleRecordPreset). One build, chosen live:
 *
 *   everyN : sample 1 in N emulated frames. 1 = every frame (needed to see
 *            a one-frame glitch); 4 ~= 15Hz.
 *   bufMB  : 0  -> stream each ~100KB sample straight to the SD card while
 *                  recording (original behaviour; the ~1.5MB/s write slows
 *                  the game and masks transient hitches).
 *            >0 -> bufMB MB of linear RAM used as a RING buffer holding the
 *                  last N samples; flushed to SD oldest-first when recording
 *                  STOPS. No SD I/O and no screenshots during the capture,
 *                  and it never auto-stops -- so you press stop right AFTER
 *                  seeing the glitch and keep the run-up to it. 16MB / ~100KB
 *                  ~= 165 frames ~= 2.7s at 60fps. */
typedef struct { const char* label; unsigned everyN; unsigned bufMB; } RecPreset;
static const RecPreset kRecPresets[] = {
    { "15Hz>SD",  4, 0  },
    { "60Hz 8M",  1, 8  },
    { "60Hz 16M", 1, 16 },
    { "60Hz 32M", 1, 32 },
    { "30Hz 32M", 2, 32 },
};
static unsigned sRecPreset; /* index into kRecPresets */

unsigned PlatformGpu3DS_CycleRecordPreset(void) {
    if (!sRecording) /* don't change mid-capture */
        sRecPreset = (sRecPreset + 1u) % (sizeof(kRecPresets) / sizeof(kRecPresets[0]));
    return sRecPreset;
}
const char* PlatformGpu3DS_RecordPresetLabel(void) {
    return kRecPresets[sRecPreset].label;
}

/* RAM-buffer mode (sRecBuf != NULL): a fixed-slot RING buffer. Every frame
 * overwrites the oldest slot, so recording never auto-stops and never
 * overwrites a good take by racing an auto-stop -- on stop you get the last
 * N seconds that fit, flushed to SD oldest-first. Slots are padded to
 * kRecordSampleMaxBytes in RAM; only the real sample bytes are written to
 * the file, so its stride is unchanged and existing parsers need no
 * change. */
static u8* sRecBuf;
static size_t sRecBufCap;
static size_t sRecSlotSize;     /* RAM stride per slot */
static unsigned sRecSlots;      /* slot capacity of the ring */
static unsigned sRecHead;       /* next slot to write */
static unsigned sRecCount;      /* valid slots, 0..sRecSlots */
static size_t sRecSlotUsed;     /* bytes into the in-progress slot */
static size_t sRecSampleBytes;  /* real size of a completed sample (constant) */
static char sRecLastFile[32];   /* basename of the last flushed recording */
static unsigned sRecStreamIndex = 1u; /* slot number of a streaming capture */

const char* PlatformGpu3DS_RecordLastFile(void) { return sRecLastFile; }

/* One sample chunk: into the current ring slot if buffering, else straight
 * to the open file. */
static void RecWrite(const void* p, size_t n) {
    if (sRecBuf) {
        u8* base = sRecBuf + (size_t)sRecHead * sRecSlotSize;
        if (sRecSlotUsed + n <= sRecSlotSize) {
            memcpy(base + sRecSlotUsed, p, n);
            sRecSlotUsed += n;
        }
    } else if (sRecFile) {
        fwrite(p, 1, n, sRecFile);
    }
}

bool PlatformGpu3DS_IsRecording(void) { return sRecording; }

/* The OAM census lives in port_oam_census.c so the host test target can
 * feed it synthetic OAM -- see platform/3ds/tests/oam_census_test.c for
 * what it covers and why it earned tests. */
static void OamCensus(unsigned* outTotal, unsigned* outVisible, unsigned* outAffine) {
    extern u16 gOamMem[0x400 / 2];
    Port_OamCensus((const uint16_t*)gOamMem, outTotal, outVisible, outAffine);
}

/* The display settings a capture was taken under. Recording these is what
 * makes two captures comparable: the 2026-09-04 round asked whether frame
 * cost scales with pixels (display style) or with quad count, and the four
 * captures taken to answer it could not be told apart afterwards, because
 * nothing in the file said which style each one used. Inferring the style
 * from the frame cost would have been circular -- the cost is the thing
 * under measurement.
 *
 * The FX grade, bezel and FPS overlay are here for the same reason: each
 * adds work to the frame, so a capture with any of them on is not
 * comparable with one without.
 *
 *   bits 0-1   display style (0 = PIXEL PERFECT, see Port_Config_*)
 *   bits 2-3   aspect ratio (2 = STRETCH)
 *   bits 4-10  3D slider position x100, 0..100
 *   bit  11    FPS overlay on
 *   bit  12    HUD drawn outside the frame
 *   bit  13    GBA bezel on
 *   bits 14-16 GBA screen-FX grade (0 = off)
 *   bit  17    Old3DS runtime profile forced
 *   bit  18    the frame was drawn by the GPU renderer (clear = CPU fallback,
 *              in which case the draw-call census describes a stale frame)
 *   bits 19-21 GBA screen-FX LCD grid level (0 = off)
 *   bits 22-24 GBA screen-FX vignette level (0 = off)
 *   bits 25-26 block pass mode (0 per-tile, 1 = 16x16, 2 = 16x16 + 32x32);
 *              always 0 since the block passes were removed
 *
 * The grid and vignette levels are here because they share the grade's cost
 * model -- all three bake into one mask drawn as a single alpha-blended quad
 * per eye, so none of them moves drawCount or drawnPixels, and a capture
 * that leaves the grid out silently merges grid-on and grid-off frames (the
 * grid adds ~1/3 screen of blended framebuffer read-modify-write per eye).
 */
static uint32_t PackCaptureFlags(void) {
    extern int Port_Config_Get3DSDisplayStyle(void);
    extern int Port_Config_Get3DSAspectRatio(void);
    extern bool Port_Config_GetShowFps(void);
    extern bool Port_Config_GetHudOutside(void);
    extern bool Port_Config_GetGbaBezel(void);
    extern int Port_Config_GetGbaFxGrade(void);
    extern int Port_Config_GetGbaFxGrid(void);
    extern int Port_Config_GetGbaFxVignette(void);
    extern bool Port_PPU_3DS_LastFrameUsedGpu(void);
    extern bool Platform3DS_IsNew3DS(void);

    const float slider = PlatformGpu3DS_Get3DSlider();
    uint32_t sliderX100 = (uint32_t)(slider * 100.0f + 0.5f);
    if (sliderX100 > 100u) sliderX100 = 100u;

    return ((uint32_t)Port_Config_Get3DSDisplayStyle() & 3u)
         | (((uint32_t)Port_Config_Get3DSAspectRatio() & 3u) << 2)
         | (sliderX100 << 4)
         | ((uint32_t)(Port_Config_GetShowFps() ? 1u : 0u) << 11)
         | ((uint32_t)(Port_Config_GetHudOutside() ? 1u : 0u) << 12)
         | ((uint32_t)(Port_Config_GetGbaBezel() ? 1u : 0u) << 13)
         | (((uint32_t)Port_Config_GetGbaFxGrade() & 7u) << 14)
         | ((uint32_t)(Platform3DS_IsNew3DS() ? 0u : 1u) << 17)
         | ((uint32_t)(Port_PPU_3DS_LastFrameUsedGpu() ? 1u : 0u) << 18)
         | (((uint32_t)Port_Config_GetGbaFxGrid() & 7u) << 19)
         | (((uint32_t)Port_Config_GetGbaFxVignette() & 7u) << 22);
         /* bits 25-26: the block pass mode, always 0 since the block passes
          * were removed (the layer maps replaced them). */
}

/* Packs Port_GpuRenderer_GetLastFrameDrawStats' flags into one word for the
 * recorders. Layout is shared by mzm-perf.bin's PerfSample.rendererFlags
 * and mzm-rec.bin's header word 14 -- keep both sides of
 * docs/3ds-debug-tools.md in step with this. */
static uint32_t PackRendererFlags(const PortGpuRendererDrawStats* st) {
    return (uint32_t)st->scissorPasses
         | ((uint32_t)(st->windowActive ? 1u : 0u) << 8)
         | ((uint32_t)(st->hazeActive ? 1u : 0u) << 9)
         | ((uint32_t)(st->eyesRendered & 3u) << 10)
         | ((uint32_t)(st->layerComposes & 7u) << 12)
         | ((uint32_t)(st->layerCacheOn ? 1u : 0u) << 15)
         /* hazeTiles is capped at HAZE_MAX_TILES (21*34 = 714), so 12 bits
          * hold it with room to spare and bits 28-29 are free for the haze
          * mode. Bits 0-15 are all spoken for; scissorPasses nominally owns
          * the whole low byte and stays that way rather than being narrowed
          * under a layout mzm-rec.bin's header word 14 also uses. */
         | ((uint32_t)(st->hazeTiles & 0x0FFFu) << 16)
         | ((uint32_t)(st->hazeMode & 3u) << 28);
}

/* ---- Perf-only frame-time recorder (issue #20) ----------------------
 * The full scene recorder above writes ~100KB/sample to SD (~1.5MB/s
 * sustained), which by itself slows the whole game down -- masking exactly
 * the kind of transient hitch we're hunting. This sibling records ONLY a
 * 16-byte entry per emulated frame into a RAM ring buffer (no SD I/O while
 * running, no screenshots): frame counter, wall-clock duration of that
 * frame in microseconds, the OAM census and the renderer's draw-call
 * census -- enough to say not just THAT a frame missed the 16.67ms budget
 * but what it spent the time on, without the SD writes that make the full
 * scene recorder's own numbers meaningless.
 * The debug tools menu toggles it; on stop the buffer is flushed to
 * <game folder>/debug/mzm-perf-NN.bin
 * as a PerfFileHeader followed by a flat little-endian array of PerfSample.
 * 60 samples/sec * 40s capacity = 2400 entries * 64B = ~154KB linear. ---- */
typedef struct {
    uint32_t magic;       /* 'MZP7'; 'MZP6' has 8 phases and no counts, 'MZP5' no phases */
    uint32_t sampleSize;  /* sizeof(PerfSample), so a parser can stride safely */
    uint32_t sampleCount;
    uint32_t reserved;
} PerfFileHeader;
typedef struct {
    uint32_t frameCounter;
    uint32_t durationUs;
    uint32_t spriteCount;        /* non-disabled OAM entries -- see OamCensus */
    uint32_t visibleSpriteCount; /* of those, actually intersecting 240x160 */
    uint32_t affineSpriteCount;
    /* -- everything below is backfilled from PlatformGpu3DS_EndBottom, see
     *    PerfBackfillPresentedFrame: at tick time this frame has not been
     *    rendered yet, and reading the counters there recorded the PREVIOUS
     *    frame's cost against this frame's duration. --------------------- */
    uint32_t drawingTimeX100;    /* citro3d C3D_GetDrawingTime, 1/100 ms (GPU) */
    uint32_t processingTimeX100; /* citro3d C3D_GetProcessingTime, 1/100 ms (GPU) */
    uint32_t cpuTileX100;        /* renderer CPU: VRAM read + tile decode + sort */
    uint32_t cpuUploadX100;      /* renderer CPU: atlas upload */
    uint32_t cpuDrawX100;        /* renderer CPU: draw-call submission, all eyes */
    uint32_t drawCount;          /* draw calls, summed over every eye drawn */
    uint32_t bgItems;            /* collected BG tile quads, ONE eye */
    uint32_t objItems;           /* collected OBJ subtile quads, ONE eye */
    uint32_t blendTransitions;
    uint32_t rendererFlags;      /* PackRendererFlags */
    uint32_t captureFlags;       /* PackCaptureFlags -- the settings in force */
    /* Device pixels the frame's quads covered, over every eye. Added with
     * magic 'MZP4': quad count stopped explaining frame time once step A cut
     * quads 6x for 5% of the frame, and this is the number that says whether
     * pixels explain it instead. Parsers stride by the header's sampleSize,
     * so an older file still reads -- it just has no such column. */
    uint32_t drawnPixels;
    /* Added with magic 'MZP5': CPU time C3D_FrameBegin spent waiting for the
     * GPU to finish the previous frame, 1/100 ms. The renderer collects
     * before that wait, so this is the GPU work the CPU could not hide. */
    uint32_t gpuWaitX100;
    /* Added with magic 'MZP6': CPU time per PerfPhase (platform_gpu_3ds.h),
     * 1/100 ms, over the frame this sample describes. */
    uint32_t phaseX100[PERF_PHASE_COUNT];
    /* 'MZP7': PerfCounter work counts (platform_gpu_3ds.h); phaseX100 also
     * grew from 8 to 11 entries with the BG breakdown. */
    uint32_t counts[PERF_COUNT_COUNT];
} PerfSample;
/* 40 seconds is far longer than any hitch hunt needs and keeps the linear
 * allocation in the same ballpark as the earlier, smaller samples. */
static const unsigned kPerfMaxSamples = 2400; /* ~40s at 60fps */
static bool sPerfRecording;
static PerfSample* sPerfSamples;
static unsigned sPerfCount;
static u64 sPerfLastTick;

bool PlatformGpu3DS_IsPerfRecording(void) { return sPerfRecording; }

void PlatformGpu3DS_TogglePerfRecording(void) {
    if (sPerfRecording) {
        extern void Port_DebugLog(const char* msg);
        char msg[80];
        char perfPath[256];
        /* Round-robin the slot within the session so back-to-back captures
         * land on mzm-perf-01, -02, -03, ... without leaving the game. The
         * first of the session picks a free slot (Port_DebugFiles_NextPath's
         * own choice); after that just advance, wrapping at PORT_KEEP_PERF.
         * A plain in-memory counter, because SD st_mtime is unreliable here
         * so "least recently modified" collapses to slot 1 every time. */
        static unsigned sPerfSlot; /* 0 until the first capture of the session */
        if (sPerfSlot == 0) {
            sPerfSlot = Port_DebugFiles_NextSetIndex("mzm-perf", ".bin", PORT_KEEP_PERF);
        } else {
            sPerfSlot = (sPerfSlot % PORT_KEEP_PERF) + 1u;
        }
        if (!Port_DebugFiles_SetPath("mzm-perf", sPerfSlot, ".bin", perfPath, sizeof(perfPath)))
            snprintf(perfPath, sizeof(perfPath), PORT_DEBUG_DIR "/mzm-perf-01.bin");
        snprintf(msg, sizeof(msg), "PERF REC STOP: %u frames -> %s", sPerfCount, perfPath);
        Port_DebugLog(msg);
        FILE* f = fopen(perfPath, "wb");
        if (f) {
            const PerfFileHeader fh = {
                .magic = 0x3750 << 16 | 0x5A4D, /* 'MZP7' */
                .sampleSize = (uint32_t)sizeof(PerfSample),
                .sampleCount = sPerfCount,
                .reserved = 0,
            };
            fwrite(&fh, sizeof(fh), 1, f);
            if (sPerfSamples && sPerfCount) fwrite(sPerfSamples, sizeof(PerfSample), sPerfCount, f);
            fclose(f);
        }
        linearFree(sPerfSamples);
        sPerfSamples = NULL;
        sPerfCount = 0;
        sPerfRecording = false;
        return;
    }
    if (!sPerfSamples) sPerfSamples = (PerfSample*)linearAlloc(kPerfMaxSamples * sizeof(PerfSample));
    if (!sPerfSamples) {
        extern void Port_DebugLog(const char* msg);
        Port_DebugLog("PERF REC: linearAlloc FAILED (need 154KB linear heap)");
        return;
    }
    sPerfCount = 0;
    sPerfLastTick = 0;
    sPerfRecording = true;
}

void PlatformGpu3DS_PerfRecordTick(void) {
    if (!sPerfRecording || !sPerfSamples) return;

    const u64 now = Platform3DS_SystemTick();
    u64 durTicks = 0;
    if (sPerfLastTick != 0) durTicks = now - sPerfLastTick;
    sPerfLastTick = now;

    if (sPerfCount >= kPerfMaxSamples) {
        PlatformGpu3DS_TogglePerfRecording(); /* flush + stop at capacity */
        return;
    }

    unsigned spriteCount = 0;
    unsigned visibleSpriteCount = 0;
    unsigned affineSpriteCount = 0;
    OamCensus(&spriteCount, &visibleSpriteCount, &affineSpriteCount);

    /* Only what is already known at tick time. This runs BEFORE the frame
     * is rendered and presented, so every cost field is left at zero here
     * and filled in by PerfBackfillPresentedFrame below -- reading the
     * renderer / citro3d counters at this point recorded the PREVIOUS
     * frame's cost against this frame's duration, which showed up in the
     * 2026-09-04 capture as 2-vblank frames reporting LOWER GPU times than
     * 1-vblank ones. */
    const u64 ticksPerSec = Platform3DS_TicksPerSecond();
    sPerfSamples[sPerfCount++] = (PerfSample){
        .frameCounter = sStats.frames,
        .durationUs = (uint32_t)((durTicks * 1000000ull) / (ticksPerSec ? ticksPerSec : 1)),
        .spriteCount = spriteCount,
        .visibleSpriteCount = visibleSpriteCount,
        .affineSpriteCount = affineSpriteCount,
    };
}

/* Fills the in-flight sample's cost fields once the frame it describes has
 * actually been rendered and presented. Called at the very end of
 * PlatformGpu3DS_EndBottom, after sStats' citro3d counters are refreshed.
 * A frame that gets sampled but never presented keeps its zeroed cost
 * fields, which is the honest answer for it. */
static void PerfBackfillPresentedFrame(void) {
    if (!sPerfRecording || !sPerfSamples || sPerfCount == 0) return;
    PerfSample* sample = &sPerfSamples[sPerfCount - 1];
    if (sample->drawCount != 0u || sample->drawingTimeX100 != 0u) return; /* already filled */

    PortGpuRendererDrawStats draw = { 0 };
    Port_GpuRenderer_GetLastFrameDrawStats(&draw);

    /* citro3d can hand back negatives outside an active frame; clamp so the
     * recorded u32 stays parseable (same guard as the scene recorder). */
    const float drawMs = sStats.drawingTime;
    const float procMs = sStats.processingTime;
    sample->drawingTimeX100 = (drawMs > 0.0f) ? (uint32_t)(drawMs * 100.0f) : 0u;
    sample->processingTimeX100 = (procMs > 0.0f) ? (uint32_t)(procMs * 100.0f) : 0u;
    sample->cpuTileX100 = draw.cpuTileX100;
    sample->cpuUploadX100 = draw.cpuUploadX100;
    sample->cpuDrawX100 = draw.cpuDrawX100;
    sample->drawCount = draw.drawCount;
    sample->bgItems = draw.bgItems;
    sample->objItems = draw.objItems;
    sample->blendTransitions = draw.blendTransitions;
    sample->drawnPixels = draw.drawnPixels;
    sample->gpuWaitX100 = (uint32_t)(sLastGpuWaitMs * 100.0f);
    for (int p = 0; p < PERF_PHASE_COUNT; ++p)
        sample->phaseX100[p] = (uint32_t)((double)sPhaseTicks[p] * 100.0 / CPU_TICKS_PER_MSEC);
    for (int c = 0; c < PERF_COUNT_COUNT; ++c) sample->counts[c] = sPerfCounts[c];
    sample->rendererFlags = PackRendererFlags(&draw);
    sample->captureFlags = PackCaptureFlags();
}

void PlatformGpu3DS_ToggleRecording(void) {
    if (sRecording) {
        sRecording = false;
        if (sRecBuf) {
            /* Flush the ring oldest-first into a rotating slot. This used
             * to walk mzm-rec.bin, -2, -3 ... up to -99 and never prune,
             * i.e. up to ~800MB of recordings that only ever accumulated;
             * now the oldest of PORT_KEEP_RECORDINGS is reused. */
            char path[256];
            if (!Port_DebugFiles_NextPath("mzm-rec", ".bin", PORT_KEEP_RECORDINGS, path, sizeof(path)))
                __builtin_snprintf(path, sizeof(path), PORT_DEBUG_DIR "/mzm-rec-01.bin");
            /* A RAM capture writes no -shot-*.rgb. If this slot number was
             * last used by a STREAMING capture, its screenshots are still on
             * the card and read as if they belong to this .bin (they don't --
             * different scene, different time). Drop them. */
            {
                size_t plen = 0; while (path[plen]) ++plen;
                if (plen > 4) {
                    char shot[272];
                    int miss = 0;
                    for (unsigned u = 0; u < kRecordMaxSamples && miss < 32; ++u) {
                        __builtin_snprintf(shot, sizeof(shot), "%.*s-shot-%04u.rgb",
                                           (int)(plen - 4), path, u);
                        miss = (remove(shot) == 0) ? 0 : (miss + 1);
                    }
                }
            }
            FILE* f = fopen(path, "wb");
            if (f) {
                unsigned first = (sRecCount < sRecSlots) ? 0u : sRecHead;
                for (unsigned i = 0; i < sRecCount; ++i) {
                    unsigned s = (first + i) % sRecSlots;
                    fwrite(sRecBuf + (size_t)s * sRecSlotSize, 1, sRecSampleBytes, f);
                }
                fclose(f);
            }
            __builtin_snprintf(sRecLastFile, sizeof(sRecLastFile), "%s", path + sizeof(PORT_DEBUG_DIR) /* skip "<debug dir>/" */);
            extern void Port_DebugLog(const char* msg);
            char msg[96];
            __builtin_snprintf(msg, sizeof(msg), "REC: %u frames (%u KB) -> %s",
                               sRecCount, (unsigned)((sRecCount * sRecSampleBytes) >> 10), sRecLastFile);
            Port_DebugLog(msg);
            linearFree(sRecBuf);
            sRecBuf = NULL;
            sRecBufCap = sRecSlotSize = 0;
            sRecSlots = sRecHead = sRecCount = 0;
            sRecSlotUsed = sRecSampleBytes = 0;
        } else if (sRecFile) {
            fclose(sRecFile);
            sRecFile = NULL;
        }
        return;
    }

    sRecFrameCounter = 0;
    sRecSampleCount = 0;
    sRecLastTick = 0;
    sRecLastFrameTicks = 0;

    const unsigned bufMB = kRecPresets[sRecPreset].bufMB;
    if (bufMB > 0) {
        sRecBufCap = (size_t)bufMB << 20;
        sRecSlotSize = kRecordSampleMaxBytes;
        sRecSlots = (unsigned)(sRecBufCap / sRecSlotSize);
        sRecHead = sRecCount = 0;
        sRecSlotUsed = sRecSampleBytes = 0;
        sRecBuf = (sRecSlots > 0) ? (u8*)linearAlloc(sRecBufCap) : NULL;
        if (sRecBuf) { sRecording = true; return; }
        /* Alloc failed: fall through to streaming so a capture still happens. */
        extern void Port_DebugLog(const char* msg);
        Port_DebugLog("REC: RAM buffer alloc failed, streaming to SD instead");
        sRecBufCap = sRecSlotSize = 0;
        sRecSlots = 0;
    }

    /* Streaming mode: the same rotating slot scheme as the buffered flush,
     * chosen up front since samples are appended as they are captured. The
     * companion screenshots below are named from sRecStreamIndex so a
     * fetched set stays matched to the recording it belongs to. */
    char streamPath[256];
    sRecStreamIndex = Port_DebugFiles_NextSetIndex("mzm-rec", ".bin", PORT_KEEP_RECORDINGS);
    if (!Port_DebugFiles_SetPath("mzm-rec", sRecStreamIndex, ".bin", streamPath, sizeof(streamPath)))
        return;
    sRecFile = fopen(streamPath, "wb");
    if (!sRecFile) return;
    __builtin_snprintf(sRecLastFile, sizeof(sRecLastFile), "%s", streamPath + sizeof(PORT_DEBUG_DIR) /* skip "<debug dir>/" */);
    sRecording = true;
}

/* Wall-clock cost of the last PlatformGpu3DS_RecordTick that actually wrote a
 * sample, microseconds. Shown next to the on-screen REC dot so a "recording
 * drags the game" report can be pinned to this path or ruled out. */
static uint32_t sLastRecTickUs;
uint32_t PlatformGpu3DS_LastRecTickUs(void) { return sLastRecTickUs; }
/* true = writing to the RAM ring; false = streaming to SD (either the
 * chosen preset, or a preset that asked for RAM but whose linearAlloc
 * failed and silently fell back). Lets the on-screen dot show which. */
bool PlatformGpu3DS_RecordingToRam(void) { return sRecording && sRecBuf != NULL; }

void PlatformGpu3DS_RecordTick(void) {
    if (!sRecording) return;

    const u64 now = Platform3DS_SystemTick();
    if (sRecLastTick != 0) sRecLastFrameTicks = now - sRecLastTick;
    sRecLastTick = now;

    if (sRecFrameCounter++ % kRecPresets[sRecPreset].everyN != 0) return;
    const u64 tickStart = now;

    if (sRecBuf) {
        /* Ring buffer: start a fresh slot, overwriting the oldest. Never
         * auto-stops -- the user stops when they've seen the glitch. */
        sRecSlotUsed = 0;
    } else if (sRecSampleCount >= kRecordMaxSamples) {
        PlatformGpu3DS_ToggleRecording(); /* streaming cap */
        return;
    }
    sRecSampleCount++;

    extern u8 gIoMem[0x400];
    extern u16 gBgPltt[256];
    extern u16 gObjPltt[256];
    extern u16 gOamMem[0x400 / 2];
    extern u8 gVram[0x18000];
    extern void PortPpuMzm_GetSamusRecordState(uint32_t* out);
    extern void PortPpuMzm_GetClipRecordBlock(uint8_t* out);
    extern int PortPpuMzm_GetClipRecordBlockSize(void);

    /* Perf extension (issue #20): header grew from 32 to 64 bytes, magic
     * bumped 'MZMR' -> 'MZM2' so old parsers fail loudly instead of
     * misparsing. New fields: last emulated frame's wall-clock duration in
     * microseconds, citro3d drawing/processing times (hundredths of ms,
     * clamped to u32), and the OAM census.
     *
     * The affine-sprite count was added on the hypothesis that rotation/
     * scaling sprites (4 at once in a Skree explosion) were what blew the
     * frame budget. Recordings from 2026-09-04 killed it: the 45 FPS rooms
     * carried FEWER visible sprites than the 60 FPS one (9 against 37) and
     * no affine ones at all. What did track was BLDCNT -- alpha blending
     * with BG0 as first target in exactly the slow rooms -- hence the
     * draw-call census in words 13-15. */
    unsigned spriteCount = 0;
    unsigned visibleSpriteCount = 0;
    unsigned affineSpriteCount = 0;
    OamCensus(&spriteCount, &visibleSpriteCount, &affineSpriteCount);
    PortGpuRendererDrawStats drawStats = { 0 };
    Port_GpuRenderer_GetLastFrameDrawStats(&drawStats);

    const u64 ticksPerSec = Platform3DS_TicksPerSecond();
    const uint32_t frameUs =
        (uint32_t)((sRecLastFrameTicks * 1000000ull) / (ticksPerSec ? ticksPerSec : 1));
    PlatformGpu3DSStats stats;
    PlatformGpu3DS_GetStats(&stats);
    uint32_t drawX100 = (uint32_t)(stats.drawingTime * 100.0f);
    uint32_t procX100 = (uint32_t)(stats.processingTime * 100.0f);
    /* citro3d can report negative/absurd values outside an active frame;
     * clamp so the recorded u32 stays sane for offline parsing. */
    if (stats.drawingTime < 0.0f) drawX100 = 0;
    if (stats.processingTime < 0.0f) procX100 = 0;

    uint32_t header[2 + 6 + 8]; /* magic, frame counter, 6 Samus words, perf */
    /* 'MZM6': the clip/camera block after VRAM now also carries the montage
     * cutscene STAGE (word 13) -- several pages of one cutscene share a
     * layer config, so the layout signature alone cannot key an override to
     * one page. The grid moved another 2 bytes; magic bumped, as with
     * 'MZM5'/'MZM4'/'MZM3' before it, so an older parser fails loudly.
     * ('MZM5' added the cutscene id, 'MZM4' area/room.) */
    header[0] = 0x364D5A4Du;
    header[1] = sRecFrameCounter;
    PortPpuMzm_GetSamusRecordState(&header[2]);
    header[8]  = frameUs;
    header[9]  = drawX100;
    header[10] = procX100;
    header[11] = spriteCount;
    header[12] = affineSpriteCount;
    /* The three words that were reserved in 'MZM2' now carry the draw-call
     * census (issue #20). The magic stays 'MZM4': nothing moved and no
     * existing field changed meaning, so older parsers (the layer workbench
     * included) keep reading these recordings -- they just see the three
     * words they were already ignoring stop being zero. */
    header[13] = drawStats.drawCount;
    header[14] = drawStats.blendTransitions;
    /* Bits 0-7 visible sprites (0..128), 8-9 scissor passes, 10 window
     * active, 11 haze active, 16-31 haze tile count. Not PackRendererFlags:
     * that layout puts hazeTiles at bit 16 already and leaves no room below
     * it for the sprite count, so the two words are packed differently on
     * purpose -- see docs/3ds-debug-tools.md. */
    /* No room for PackCaptureFlags here -- all three formerly-reserved
     * words are spoken for. A scene recording is for looking at frames
     * rather than comparing costs between captures, so the settings matter
     * less; mzm-perf.bin carries them. */
    header[15] = (visibleSpriteCount & 0xFFu)
               | ((uint32_t)(drawStats.scissorPasses & 3u) << 8)
               | ((uint32_t)(drawStats.windowActive ? 1u : 0u) << 10)
               | ((uint32_t)(drawStats.hazeActive ? 1u : 0u) << 11)
               | ((uint32_t)(drawStats.eyesRendered & 3u) << 12)
               | ((drawStats.hazeTiles & 0xFFFFu) << 16);

    RecWrite(header, sizeof(header));
    RecWrite(gIoMem, sizeof(gIoMem));
    RecWrite(gBgPltt, sizeof(gBgPltt));
    RecWrite(gObjPltt, sizeof(gObjPltt));
    RecWrite(gOamMem, sizeof(gOamMem));
    RecWrite(gVram, sizeof(gVram));

    /* Clip/camera block -- see PortPpuMzm_GetClipRecordBlock. Small (220
     * bytes against the sample's ~99KB), so it costs nothing next to VRAM. */
    {
        uint8_t clipBlock[256];
        int clipSize = PortPpuMzm_GetClipRecordBlockSize();
        if (clipSize > (int)sizeof(clipBlock)) clipSize = (int)sizeof(clipBlock);
        memset(clipBlock, 0, sizeof(clipBlock));
        PortPpuMzm_GetClipRecordBlock(clipBlock);
        RecWrite(clipBlock, (size_t)clipSize);
    }

    if (sRecBuf) {
        /* Slot complete: advance the ring. All samples are the same size,
         * so record it once for the flush. */
        sRecSampleBytes = sRecSlotUsed;
        sRecHead = (sRecHead + 1u) % sRecSlots;
        if (sRecCount < sRecSlots) sRecCount++;
    }

    /* sRecSampleCount was already incremented above, so sample #1 (the
     * first one written this recording) always gets a screenshot too. File
     * name embeds the sample index so it lines up with mzm-rec.bin's Nth
     * record (0-indexed, matching the Python splitting snippet in
     * docs/3ds-debug-tools.md) without needing to also parse frameCounter.
     * Skipped in RAM-buffer mode: DumpOneTarget does a GPU sync + SD write,
     * exactly the stall the buffer exists to avoid. */
    if (!sRecBuf && (sRecSampleCount - 1) % kRecordScreenshotEverySamples == 0) {
        char path[256];
        char suffix[32];
        snprintf(suffix, sizeof(suffix), "-shot-%04u.rgb", sRecSampleCount - 1);
        if (Port_DebugFiles_SetPath("mzm-rec", sRecStreamIndex, suffix, path, sizeof(path)))
            DumpOneTarget(sTopTarget, path);
    }

    {
        const u64 ticks = Platform3DS_SystemTick() - tickStart;
        const u64 tps = Platform3DS_TicksPerSecond();
        sLastRecTickUs = (uint32_t)((ticks * 1000000ull) / (tps ? tps : 1));
    }
}

void PlatformGpu3DS_DumpScreens(void) {
    if (!sReady) return;

    /* Every file of one press shares a slot number, so a fetched set is
     * unambiguously one capture: mzm-dump-03-left.rgb, -03-vram.bin, ...
     * Probing on "-left.rgb" since that is written first and always. All
     * eleven files used to be fixed names overwritten in place, which made
     * two dumps impossible to compare -- and worse, a set could silently
     * end up a mix of two presses if a fetch landed in between (that
     * actually happened during the issue #17 investigation, see the note in
     * docs/3ds-debug-tools.md about a "truncated/mismatched mix"). */
    const unsigned set = Port_DebugFiles_NextSetIndex("mzm-dump", "-left.rgb", PORT_KEEP_DUMPS);
    sDumpSetIndex = set;
    DumpQuiesceBegin();
    char path[256];
#define PORT_DUMP_PATH(suffix)     (Port_DebugFiles_SetPath("mzm-dump", set, (suffix), path, sizeof(path)) ? path : "")

    DumpOneTarget(sTopTarget, PORT_DUMP_PATH("-left.rgb"));
    DumpOneTarget(sTopRightTarget, PORT_DUMP_PATH("-right.rgb"));
    DumpOneTarget(sBottomTarget, PORT_DUMP_PATH("-bottom.rgb"));
    DumpQuiesceEnd();

    extern u8 gIoMem[0x400];
    extern u16 gBgPltt[256];
    extern u16 gObjPltt[256];
    extern u16 gOamMem[0x400 / 2];
    extern u8 gVram[0x18000];
    WriteBlob(PORT_DUMP_PATH("-vram.bin"), gVram, sizeof(gVram));
    WriteBlob(PORT_DUMP_PATH("-io.bin"), gIoMem, sizeof(gIoMem));
    WriteBlob(PORT_DUMP_PATH("-bgpltt.bin"), gBgPltt, sizeof(gBgPltt));
    WriteBlob(PORT_DUMP_PATH("-objpltt.bin"), gObjPltt, sizeof(gObjPltt));
    WriteBlob(PORT_DUMP_PATH("-oam.bin"), gOamMem, sizeof(gOamMem));
#undef PORT_DUMP_PATH

    PortPpuMzm_DumpSamusState();

    uint16_t dispcnt = (uint16_t)(gIoMem[0x00] | (gIoMem[0x01] << 8));
    uint16_t bg0cnt  = (uint16_t)(gIoMem[0x08] | (gIoMem[0x09] << 8));
    uint16_t bg1cnt  = (uint16_t)(gIoMem[0x0A] | (gIoMem[0x0B] << 8));
    uint16_t bg2cnt  = (uint16_t)(gIoMem[0x0C] | (gIoMem[0x0D] << 8));
    uint16_t bg3cnt  = (uint16_t)(gIoMem[0x0E] | (gIoMem[0x0F] << 8));
    uint16_t bldcnt  = (uint16_t)(gIoMem[0x50] | (gIoMem[0x51] << 8));

    char msg[256];
    snprintf(msg, sizeof(msg),
        "DUMP: DISPCNT=%04X BG0CNT=%04X(prio=%u) BG1CNT=%04X(prio=%u) BG2CNT=%04X(prio=%u) BG3CNT=%04X(prio=%u) BLDCNT=%04X",
        dispcnt, bg0cnt, bg0cnt & 3, bg1cnt, bg1cnt & 3, bg2cnt, bg2cnt & 3, bg3cnt, bg3cnt & 3, bldcnt);
    extern void Port_DebugLog(const char* msg);
    Port_DebugLog(msg);

    /* Flush any buffered Port_DebugLogBuffered() lines (e.g. kraid.c's
     * KraidSync/KraidPrimary diagnostics) now, so whatever led up to this
     * capture is on disk instead of sitting in RAM. */
    extern void Port_DebugLogFlush(void);
    Port_DebugLogFlush();
}

