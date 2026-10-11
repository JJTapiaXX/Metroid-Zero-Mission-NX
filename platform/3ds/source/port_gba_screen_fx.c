/*
 * GBA-LCD look post-processing for the top screen -- see port_gba_screen_fx.h.
 *
 * Like platform_gpu_3ds.c this file talks to citro2d/citro3d and therefore
 * must NOT pull in any GBA-port game header (their u32 typedef clashes with
 * libctru's). Effect levels come in through the plain int config accessors
 * in port_ppu_mzm.c.
 *
 * Cost model (this runs at the tail of every frame, serial with
 * C3D_FrameEnd, so its fill-rate lands straight on frame time -- and the
 * heaviest rooms on an Old 3DS have almost no spare budget):
 *
 *   - The three effects (colour grade, LCD grid, vignette) do not depend on
 *     the game image, only on the config levels. They are baked into ONE
 *     screen-sized RGBA mask (black tint, per-pixel alpha) whenever a level
 *     changes -- never per frame.
 *   - Each frame is then a SINGLE alpha-blended quad per eye, with the alpha
 *     test discarding fully-transparent texels before they ever read the
 *     framebuffer. Grade-off / grid-only frames touch ~1/3 of the screen;
 *     grade-on frames touch most of it but still in one pass, not three.
 */

#include "port_gba_screen_fx.h"

#include <3ds.h>
#include <citro2d.h>
#include <string.h>

/* Puts TEV unit 0 back to "output the vertex colour" and blanks units 1..5. */
extern void PlatformGpu3DS_ResetSolidTexEnv(void);

/* Effect levels: 0 = OFF, 1 = LOW, 2 = MEDIUM, 3 = HIGH. */
extern int Port_Config_GetGbaFxGrade(void);
extern int Port_Config_GetGbaFxGrid(void);
extern int Port_Config_GetGbaFxVignette(void);
/* Where the numbers overlay sits: 0 = OFF, 1 = BOTTOM-LEFT,
 * 2 = BOTTOM-RIGHT, 3 = TOP-LEFT, 4 = TOP-RIGHT. */
extern int Port_Config_GetFpsPosition(void);
/* Rectangle the emulated GBA picture occupies on the 400x240 top screen --
 * varies with display style / aspect. The effects only touch this rect (the
 * scanline pitch follows the GBA-pixel size, and the vignette rings the
 * picture, not the black letterbox). */
extern void PlatformGpu3DS_GetTopImageRect(int* x, int* y, int* w, int* h);

enum { FX_W = 400, FX_H = 240, FX_TEX_W = 512, FX_TEX_H = 256 };
enum { GBA_W = 240, GBA_H = 160 };

static C3D_Tex sFxTex;
static Tex3DS_SubTexture sFxSub;
static bool sFxReady;
static int sBakedGrade = -1, sBakedGrid = -1, sBakedVig = -1, sBakedFps = -1;
static int sBakedRect[4] = { -1, -1, -1, -1 };

/* The top-screen numbers box (PlatformGpu3DS_DrawFpsOverlay): five small lines, up to 40 px
 * wide and 52 high, 2 px from the corner, plus a small margin. Carve it out of the mask so the
 * effects never clip across it in SCALED (where it sits inside the picture). These must track
 * DrawFpsOverlay's own box layout (FPS_OVERLAY_* in platform_gpu_3ds.c). */
enum { FPS_BOX_W = 40, FPS_BOX_H = 52, FPS_BOX_PAD = 3 };
enum { FPS_LEFT_X = 2, FPS_RIGHT_X = 358, FPS_TOP_Y = 2, FPS_BOTTOM_Y = 186 };

/* Carve-out box for FPS position `pos`, padded. Returns false for OFF (or an
 * unknown position), leaving the outputs untouched. */
static bool FpsCarveBox(int pos, int* x0, int* y0, int* x1, int* y1) {
    int bx, by;
    switch (pos) {
        case 1: bx = FPS_LEFT_X;  by = FPS_BOTTOM_Y; break;
        case 2: bx = FPS_RIGHT_X; by = FPS_BOTTOM_Y; break;
        case 3: bx = FPS_LEFT_X;  by = FPS_TOP_Y;    break;
        case 4: bx = FPS_RIGHT_X; by = FPS_TOP_Y;    break;
        default: return false;
    }
    *x0 = bx - FPS_BOX_PAD;
    *y0 = by - FPS_BOX_PAD;
    *x1 = bx + FPS_BOX_W + FPS_BOX_PAD;
    *y1 = by + FPS_BOX_H + FPS_BOX_PAD;
    return true;
}

static int ClampLevel(int level) {
    if (level < 0) return 0;
    if (level > 3) return 3;
    return level;
}

/* A GPU_RGBA8 texel in the PICA's native byte order (alpha in the low byte:
 * u32 = 0xRRGGBBAA). NOT C2D_Color32, which packs 0xAABBGGRR for citro2d's
 * vertex colours / the atlas's byte-swizzle TEV -- this mask is sampled by
 * citro2d's default (non-swizzling) env, so it must be native order or the
 * alpha byte is read as red and the whole effect vanishes. */
static u32 Rgba8(u8 r, u8 g, u8 b, u8 a) {
    return ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | (u32)a;
}

/* Byte offset of texel (x,y) in the 3DS tiled layout: row-major 8x8 tiles,
 * Morton order within each tile. */
static u32 SwizzleOffset(u32 x, u32 y) {
    u32 tile = (y >> 3) * (FX_TEX_W >> 3) + (x >> 3);
    u32 in = (x & 1u) | ((y & 1u) << 1) | ((x & 2u) << 1)
           | ((y & 2u) << 2) | ((x & 4u) << 2) | ((y & 4u) << 3);
    return tile * 64u + in;
}

/* GBA COLOR is a brightness/contrast correction, not a colour cast: AGB
 * games (Zero Mission included) were authored bright to be legible on the
 * unlit reflective panel, so on a backlit screen they look washed out.
 * Darkening toward black -- mix(dst, 0, a) == dst * (1 - a) -- pulls that
 * artificial brightness back while keeping hue and relative contrast. */
static u8 GradeBaseAlpha(int level) {
    static const u8 k[4] = { 0, 14, 26, 40 };   /* ~ -5% / -10% / -16% brightness */
    return k[level];
}

/* One thin (1px) dark line on every other GBA-pixel row boundary, aligned
 * to the GBA grid at ANY scale -- `row` is which GBA row this screen pixel
 * falls in, and the line sits on the first screen pixel of that row. Only
 * the alpha changes with the level; the geometry never does. HIGH adds the
 * same on every other GBA column (an LCD grille). */
static u8 GridBlackAlphaAt(int level, int lx, int ly, int rw, int rh) {
    if (level == 0) return 0;
    /* HIGH keeps MEDIUM's line strength and just adds the vertical grille. */
    static const u8 kRow[4] = { 0, 55, 95, 95 };
    static const u8 kCol[4] = { 0, 0, 0, 95 };

    const int row  = ly * GBA_H / rh;
    const int prow = ly > 0 ? (ly - 1) * GBA_H / rh : -1;
    if (row != prow && (row & 1) == 0) return kRow[level];

    if (kCol[level]) {
        const int col  = lx * GBA_W / rw;
        const int pcol = lx > 0 ? (lx - 1) * GBA_W / rw : -1;
        if (col != pcol && (col & 1) == 0) return kCol[level];
    }
    return 0;
}

/* Elliptical vignette with a smoothstep falloff from the picture centre --
 * no hard rectangular edge, and it scales with the picture so PIXEL PERFECT
 * gets a small oval and SCALED a big one. */
static u8 VignetteBlackAlphaAt(int level, int lx, int ly, int w, int h) {
    if (level == 0) return 0;
    static const u8 kMax[4] = { 0, 30, 55, 80 };
    const float nx = ((float)lx - w * 0.5f) / (w * 0.5f);
    const float ny = ((float)ly - h * 0.5f) / (h * 0.5f);
    float r2 = nx * nx + ny * ny;                 /* 0 at centre, ~2 at corners */
    const float inner = 0.28f, outer = 1.30f;
    if (r2 <= inner) return 0;
    float t = (r2 - inner) / (outer - inner);
    if (t > 1.0f) t = 1.0f;
    t = t * t * (3.0f - 2.0f * t);                /* smoothstep */
    return (u8)(kMax[level] * t);
}

/* Bake the whole overlay into the mask, once, when a level or the picture
 * rectangle changes. Everything darkens toward black; texels outside the
 * picture stay transparent (the alpha test drops them -- no black on the
 * letterbox / FPS overlay). */
/* Even with every effect on HIGH the picture must stay clearly readable, so
 * the summed darkening is capped well short of opaque. */
#define FX_MAX_ALPHA 165

static void BakeMask(int grade, int grid, int vig, int fpsPos, const int rect[4]) {
    if (!sFxReady) return;
    const int rx = rect[0], ry = rect[1], rw = rect[2], rh = rect[3];
    const int gBase = GradeBaseAlpha(grade);
    u32* data = (u32*)sFxTex.data;
    int fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;
    const bool hasFpsBox = FpsCarveBox(fpsPos, &fx0, &fy0, &fx1, &fy1);

    for (int y = 0; y < FX_H; ++y) {
        for (int x = 0; x < FX_W; ++x) {
            const int lx = x - rx, ly = y - ry;
            u32 texel = 0u;
            const bool inFps = hasFpsBox && x >= fx0 && x < fx1 && y >= fy0 && y < fy1;
            if (!inFps && lx >= 0 && lx < rw && ly >= 0 && ly < rh) {
                int a = gBase
                      + GridBlackAlphaAt(grid, lx, ly, rw, rh)
                      + VignetteBlackAlphaAt(vig, lx, ly, rw, rh);
                if (a > FX_MAX_ALPHA) a = FX_MAX_ALPHA;
                if (a > 0) texel = Rgba8(0, 0, 0, (u8)a);
            }
            data[SwizzleOffset((u32)x, (u32)y)] = texel;
        }
    }
    GSPGPU_FlushDataCache(data, FX_TEX_W * FX_TEX_H * sizeof(u32));
    sBakedGrade = grade;
    sBakedGrid = grid;
    sBakedVig = vig;
    sBakedFps = fpsPos;
    sBakedRect[0] = rx; sBakedRect[1] = ry; sBakedRect[2] = rw; sBakedRect[3] = rh;
}

void PortGbaScreenFx_Init(void) {
    if (sFxReady) return;
    if (!C3D_TexInit(&sFxTex, FX_TEX_W, FX_TEX_H, GPU_RGBA8)) {
        sFxReady = false;
        return;
    }
    memset(sFxTex.data, 0, FX_TEX_W * FX_TEX_H * sizeof(u32));
    C3D_TexSetFilter(&sFxTex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&sFxTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    sFxSub.width = FX_W;
    sFxSub.height = FX_H;
    sFxSub.left = 0.0f;
    sFxSub.right = (float)FX_W / FX_TEX_W;
    sFxSub.top = 1.0f;
    sFxSub.bottom = 1.0f - (float)FX_H / FX_TEX_H;
    sFxReady = true;
    sBakedGrade = sBakedGrid = sBakedVig = sBakedFps = -1;
    sBakedRect[0] = sBakedRect[1] = sBakedRect[2] = sBakedRect[3] = -1;
}

void PortGbaScreenFx_Shutdown(void) {
    if (!sFxReady) return;
    C3D_TexDelete(&sFxTex);
    sFxReady = false;
}

bool PortGbaScreenFx_Active(void) {
    return ClampLevel(Port_Config_GetGbaFxGrade()) > 0
        || ClampLevel(Port_Config_GetGbaFxGrid()) > 0
        || ClampLevel(Port_Config_GetGbaFxVignette()) > 0;
}

static unsigned sDebugLastSubmitUs;
unsigned PortGbaScreenFx_DebugLastSubmitUs(void) { return sDebugLastSubmitUs; }

static void DrawMaskQuad(C3D_RenderTarget* target) {
    C2D_SceneBegin(target);
    PlatformGpu3DS_ResetSolidTexEnv();
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
                   GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    /* Skip fully-transparent texels before they read the framebuffer. */
    C3D_AlphaTest(true, GPU_GREATER, 0);

    /* Output the texel straight through (RGB = tint, A = baked mask). */
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

    const C2D_Image img = { .tex = &sFxTex, .subtex = &sFxSub };
    const C2D_DrawParams p = {
        .pos = { 0.0f, 0.0f, (float)FX_W, (float)FX_H },
        .center = { 0.0f, 0.0f }, .depth = 0.0f, .angle = 0.0f,
    };
    C2D_DrawImage(img, &p, NULL);
    C2D_Flush();

    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    PlatformGpu3DS_ResetSolidTexEnv();
}

void PortGbaScreenFx_PostProcessTop(C3D_RenderTarget* left, C3D_RenderTarget* right) {
    sDebugLastSubmitUs = 0;
    const int grade = ClampLevel(Port_Config_GetGbaFxGrade());
    const int grid = ClampLevel(Port_Config_GetGbaFxGrid());
    const int vig = ClampLevel(Port_Config_GetGbaFxVignette());
    if ((grade | grid | vig) == 0 || !sFxReady) return;

    const u64 t0 = svcGetSystemTick();

    const int fpsPos = Port_Config_GetFpsPosition();
    int rect[4];
    PlatformGpu3DS_GetTopImageRect(&rect[0], &rect[1], &rect[2], &rect[3]);
    if (grade != sBakedGrade || grid != sBakedGrid || vig != sBakedVig
        || fpsPos != sBakedFps
        || rect[0] != sBakedRect[0] || rect[1] != sBakedRect[1]
        || rect[2] != sBakedRect[2] || rect[3] != sBakedRect[3])
        BakeMask(grade, grid, vig, fpsPos, rect);

    /* Commit whatever the present path queued into the top target(s) first,
     * so the overlay composites over a finished image rather than joining
     * its batch (which renders through the ABGR-swizzle TEV). */
    C2D_Flush();

    if (left) DrawMaskQuad(left);
    if (right) DrawMaskQuad(right);

    sDebugLastSubmitUs =
        (unsigned)((svcGetSystemTick() - t0) * 1000000ull / SYSCLOCK_ARM11);
}
