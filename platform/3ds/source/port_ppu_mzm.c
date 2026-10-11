/*
 * PPU->GPU bridge for the Metroid: Zero Mission 3DS port. Binds
 * the port/ppu software renderer (port/ppu/src/{virtuappu,mode1}.c)
 * directly to mzm's emulated GBA memory and presents frames to the
 * screen via Citro2D/Citro3D.
 */
#include "virtuappu.h"
#include "cpu/mode1.h"
#include "port_gba_mem.h"
#include "platform_gpu_3ds.h"
#include "structs/samus.h"
#include "constants/samus.h"
#include "samus.h"
#include "gba/memory.h"
#include "structs/bg_clip.h"
#include "structs/clipdata.h"
#include "structs/scroll.h"
#include "structs/sprite.h"    /* gSpriteData, for the dump's sprite list */
#include "constants/clipdata.h"
#include "constants/block.h"
#include "constants/game_state.h"
#include "structs/game_state.h"
#include "constants/cutscene.h"       /* CUTSCENE_DATA */
#include "structs/cutscene.h"
#include "structs/tourian_escape.h"   /* TOURIAN_ESCAPE_DATA */
#include "data/shortcut_pointers.h"   /* sNonGameplayRamPointer */
#include "structs/connection.h"
#include "structs/minimap.h"
#include "structs/room.h"
#include "constants/room.h"
#include "scroll.h"            /* ScrollGetBg3Scroll, for PortPpuMzm_WideLayerDivisor */
#include "structs/color_effects.h"  /* gColorFading, for PortPpuMzm_DoorTunnel */
#include "constants/color_fading.h"
#include "constants/minimap.h"
#include "minimap.h"
#include "menus/pause_screen.h" /* PauseScreenGetMinimapData */
#include "port_haze_3ds.h"
#include "port_wide_view.h"
#include "macros.h"
#include "port_debug_tools.h" /* PORT_DEBUG_TOOLS_ACTIVE */

#ifdef PORT_GPU_TILE_RENDERER
#include "port_gpu_renderer.h"
#endif

#ifdef PORT_DEBUG_TOOLS_ACTIVE
/* Deliberately not <3ds.h> here -- it redeclares u32/s32/etc. incompatibly
 * with this project's own include/types.h (already pulled in transitively
 * above), which is a hard conflicting-types error. CPU_TICKS_PER_MSEC's
 * value (3ds/os.h: SYSCLOCK_ARM11 / 1000.0, SYSCLOCK_ARM11 = 268111856*2)
 * is a stable libctru constant, safe to inline instead of the header. */
#define PORT_PPU_PERF_CPU_TICKS_PER_MSEC (268111856.0 / 1000.0)
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void Port_DebugLog(const char* msg);

// #define PORT_VERBOSE_FRAME_LOG


/* Must match platform_gpu_3ds.c's TOP_TEXTURE_WIDTH -- the buffer
 * PlatformGpu3DS_TopBuffer() hands back is allocated at that pitch. */
#define TOP_PITCH 512
#define TOP_NATIVE_W 240

static uint32_t* sTopBuffer;
static uint32_t* sTopRightBuffer;
static uint32_t* sBottomBuffer;
static bool sReady;
static unsigned sPresentFrameCount;

/* Logic-side (CPU scanline render) output buffers, decoupled from the real
 * GPU-mapped ones above. The CPU renderer used to write straight into
 * sTopBuffer/sTopRightBuffer -- the exact buffer PlatformGpu3DS_* then
 * uploaded to the GPU -- on the SAME thread and call, so a slow/stalled GPU
 * submission (the still-unresolved bottom-screen bug's neighborhood) stalled
 * the whole agbmain() loop, which stalled audio production with it (see
 * docs/future-roadmap-and-architecture.md Fase 3 and the branch history).
 * Port_PPU_RenderFrame() (game-logic thread) now renders into one of two
 * ping-pong buffers here and publishes it; Port_PPU_GpuPresentPump() (a
 * separate present thread, main_3ds.c) copies the latest published buffer
 * into the real GPU buffer and does the actual GPU submission at its own
 * pace. Lock-free hand-off (generation counter, release/acquire), matching
 * the existing mzm-audio-ring pattern in port_mzm_audio_glue.c rather than
 * introducing the first mutex/LightLock in this tree. Two slots means the
 * present thread must keep up with one full logic-frame period (it does,
 * see port_ppu_mzm.c's Port_PPU_GpuPresentPump doc comment) or it risks
 * reading a slot the logic thread has started overwriting; a third slot
 * would close that theoretical window at the cost of more RAM and isn't
 * worth it unless it's observed in practice. */
#define LOGIC_BUFFER_SLOTS 2
static uint32_t* sLogicTop[LOGIC_BUFFER_SLOTS];
static uint32_t* sLogicTopRight[LOGIC_BUFFER_SLOTS];
static unsigned sLogicWriteSlot;
static volatile unsigned sPublishedGeneration;
static volatile unsigned sPublishedSlot;
static unsigned sConsumedGeneration;

/* platform_gpu_3ds.c calls these (FPS HUD / aspect ratio / display style
 * config, normally backed by a settings menu that doesn't exist yet). */
extern bool Platform3DS_IsNew3DS(void);
extern uint64_t osGetTime(void); /* libctru: milliseconds since epoch */
extern float osGet3DSliderState(void);

/* Rolling 1-second window: frames presented since sFpsWindowStartMs, turned
 * into a rate once the window closes. sPresentFrameCount alone (a lifetime
 * total) isn't a frame rate -- see UpdateFpsWindow(), called once per
 * presented frame below. */
static uint64_t sFpsWindowStartMs;
static unsigned sFpsWindowFrames;
static double sCurrentFps;

static void UpdateFpsWindow(void) {
    const uint64_t now = osGetTime();
    if (sFpsWindowStartMs == 0) {
        sFpsWindowStartMs = now;
        sFpsWindowFrames = 0;
        return;
    }
    ++sFpsWindowFrames;
    const uint64_t elapsed = now - sFpsWindowStartMs;
    if (elapsed >= 1000u) {
        sCurrentFps = (double)sFpsWindowFrames * 1000.0 / (double)elapsed;
        sFpsWindowStartMs = now;
        sFpsWindowFrames = 0;
    }
}

#include "port_bottom_ui_3ds.h"
#include "port_paths.h"
#include <stdio.h>

static int sFpsPosition = 1; /* 0 = OFF, 1 = BOTTOM-LEFT, 2 = BOTTOM-RIGHT, 3 = TOP-LEFT, 4 = TOP-RIGHT */
static int sAspectRatio = 1; /* 1 = ORIGINAL, 2 = STRETCH, 3 = WIDE (0 = the old retired WIDE, clamped to ORIGINAL on load) */
static int sDisplayStyle = 0; /* 0 = PIXEL PERFECT, 1 = SCALED (2 = BLUR retired -- no effect on the GPU path) */
static const char* const sConfigPath = "mzm3ds.ini";

static bool sAutoHideHud = true;
static bool sHideSpoilers = true;
static bool sHudOutside = false;
static bool sGbaBezel = false;

/* GBA-look top-screen effects, 0 = OFF, 1 = LOW, 2 = MEDIUM, 3 = HIGH.
 * Implemented in port_gba_screen_fx.c and applied by platform_gpu_3ds.c;
 * every effect at 0 is a full bypass. */
static int sGbaFxGrade = 0;
static int sGbaFxGrid = 0;
static int sGbaFxVignette = 0;

/* Frame pacing on the GPU path. 0 = ADAPTIVE: run at whatever rate the scene
 * allows, skipping the render (never the logic) to keep game speed correct
 * when a frame overruns -- fluid, variable 30..60. 1 = LOCKED 30: render
 * every other logic tick for a steady 30 Hz picture at correct game speed.
 * See Port_Bios_Halt / Port_Bios_ShouldSkipRender. */
static int sFramePacing = 0;
#ifdef PORT_DEBUG_TOOLS
/* DEBUG -> HERRAMIENTAS -> FTP SERVER as it was left: the next launch (an install over FTP
 * relaunches) starts it again by itself. Debug builds only. */
static bool sDebugFtpOn = false;
#endif

/* Button Actions:
 * 0 = NINGUNA (NONE)
 * 1 = AUTODISPARO (RAPID FIRE)
 * 2 = MORFOSFERA RAPIDA (QUICK MORPH BALL)
 * 3 = DISPARO (FIRE / B)
 * 4 = SALTAR (JUMP / A)
 * 5 = MISILES (SELECT / swap missile type)
 * 6 = APUNTAR (AIM / DIAGONAL AIM MODIFIER / KEY_L)
 * 7 = ARMAR MISILES (ARM WEAPON / KEY_R)
 * 8 = PAUSA (PAUSE / KEY_START)
 */
#define BTN_ACTION_COUNT 9

/* Remappable button indices:
 * 0=A, 1=B, 2=X, 3=Y, 4=L, 5=R, 6=ZL, 7=ZR, 8=Start, 9=Select
 * D-Pad and Circle Pad are always fixed to movement. */
#define BTN_REMAP_COUNT 10

static int sBtnRemap[BTN_REMAP_COUNT] = {
    4, /* 0: A      -> JUMP (4) */
    3, /* 1: B      -> FIRE (3) */
    1, /* 2: X      -> RAPID FIRE (1) */
    2, /* 3: Y      -> QUICK MORPH (2) */
    6, /* 4: L      -> AIM (6) */
    7, /* 5: R      -> ARM WEAPON (7) */
    6, /* 6: ZL     -> AIM (6) */
    7, /* 7: ZR     -> ARM WEAPON (7) */
    8, /* 8: Start  -> PAUSE (8) */
    5, /* 9: Select -> MISSILES (5) */
};

/* C-Stick mode: 0=OFF, 1=AIM (UP/DOWN), 2=MOVEMENT (LEFT/RIGHT), 3=ALL (4 DIRECTIONS) */
static int sCstickMode = 0;

void Port_Config_Save(void);

void Port_Config_ResetButtonMappingDefault(void) {
    sBtnRemap[0] = 4; /* A      -> JUMP (4) */
    sBtnRemap[1] = 3; /* B      -> FIRE (3) */
    sBtnRemap[2] = 1; /* X      -> RAPID FIRE (1) */
    sBtnRemap[3] = 2; /* Y      -> QUICK MORPH (2) */
    sBtnRemap[4] = 6; /* L      -> AIM (6) */
    sBtnRemap[5] = 7; /* R      -> ARM WEAPON (7) */
    sBtnRemap[6] = 6; /* ZL     -> AIM (6) */
    sBtnRemap[7] = 7; /* ZR     -> ARM WEAPON (7) */
    sBtnRemap[8] = 8; /* Start  -> PAUSE (8) */
    sBtnRemap[9] = 5; /* Select -> MISSILES (5) */
    sCstickMode = 0;  /* OFF */
    Port_Config_Save();
}

void Port_Config_Save(void) {
    FILE* file = fopen(sConfigPath, "wb");
    if (!file) return;
    fprintf(file, "# Metroid Zero Mission 3DS runtime settings\n");
    fprintf(file, "aspect_ratio=%d\n", sAspectRatio);
    fprintf(file, "display_style=%d\n", sDisplayStyle);
    fprintf(file, "show_fps=%d\n", sFpsPosition);
    fprintf(file, "auto_hide_hud=%u\n", sAutoHideHud ? 1u : 0u);
    fprintf(file, "hide_spoilers=%u\n", sHideSpoilers ? 1u : 0u);
    fprintf(file, "hud_outside=%u\n", sHudOutside ? 1u : 0u);
    fprintf(file, "gba_bezel=%u\n", sGbaBezel ? 1u : 0u);
    fprintf(file, "gba_fx_grade=%d\n", sGbaFxGrade);
    fprintf(file, "gba_fx_grid=%d\n", sGbaFxGrid);
    fprintf(file, "gba_fx_vignette=%d\n", sGbaFxVignette);
    fprintf(file, "frame_pacing=%d\n", sFramePacing);
    fprintf(file, "btn_map_a=%d\n", sBtnRemap[0]);
    fprintf(file, "btn_map_b=%d\n", sBtnRemap[1]);
    fprintf(file, "btn_map_x=%d\n", sBtnRemap[2]);
    fprintf(file, "btn_map_y=%d\n", sBtnRemap[3]);
    fprintf(file, "btn_map_l=%d\n", sBtnRemap[4]);
    fprintf(file, "btn_map_r=%d\n", sBtnRemap[5]);
    fprintf(file, "btn_map_zl=%d\n", sBtnRemap[6]);
    fprintf(file, "btn_map_zr=%d\n", sBtnRemap[7]);
    fprintf(file, "btn_map_start=%d\n", sBtnRemap[8]);
    fprintf(file, "btn_map_select=%d\n", sBtnRemap[9]);
    fprintf(file, "cstick_mode=%d\n", sCstickMode);
    {
        extern bool Port_AutoBomb_Enabled(void);
        fprintf(file, "auto_bomb_jump=%u\n", Port_AutoBomb_Enabled() ? 1u : 0u);
    }
    fprintf(file, "bottom_tab=%d\n", (int)Port_BottomUI_GetTab());
    fprintf(file, "bottom_zoom=%d\n", Port_BottomUI_GetZoom());
    fprintf(file, "bottom_area=%d\n", Port_BottomUI_GetViewArea());
    fprintf(file, "bottom_follow=%u\n", Port_BottomUI_GetFollowSamus() ? 1u : 0u);
    fprintf(file, "language=%d\n", (int)gLanguage);
    extern bool Port_RA_IsEnabled(void);
    extern bool Port_RA_IsHardcore(void);
    extern bool Port_RA_GetNotificationSound(void);
    extern bool Port_RA_GetNotifyOnTopScreen(void);
    extern const char* Port_RA_GetUsername(void);
    extern const char* Port_RA_GetToken(void);
    fprintf(file, "ra_enabled=%u\n", Port_RA_IsEnabled() ? 1u : 0u);
    fprintf(file, "ra_hardcore=%u\n", Port_RA_IsHardcore() ? 1u : 0u);
    fprintf(file, "ra_sound=%u\n", Port_RA_GetNotificationSound() ? 1u : 0u);
    fprintf(file, "ra_notify_top=%u\n", Port_RA_GetNotifyOnTopScreen() ? 1u : 0u);
    extern bool Port_Updater_GetAuto(void);
    extern bool Port_Updater_GetBeta(void);
    extern const char* Port_Updater_GetUrlOverride(void);
    fprintf(file, "update_auto=%u\n", Port_Updater_GetAuto() ? 1u : 0u);
    fprintf(file, "update_beta=%u\n", Port_Updater_GetBeta() ? 1u : 0u);
    if (Port_Updater_GetUrlOverride()[0]) fprintf(file, "update_url=%s\n", Port_Updater_GetUrlOverride());
#ifdef PORT_DEBUG_TOOLS
    fprintf(file, "debug_ftp=%u\n", sDebugFtpOn ? 1u : 0u);
#endif
    fprintf(file, "ra_username=%s\n", Port_RA_GetUsername());
    fprintf(file, "ra_token=%s\n", Port_RA_GetToken());
    fclose(file);
}

void Port_Config_Load(void) {
    FILE* file = fopen(sConfigPath, "rb");
    if (!file) return;
    char line[256];
    while (fgets(line, sizeof(line), file) != NULL) {
        char key[64];
        int val = 0;
        if (line[0] == '#') continue;
        {
            char url[200];
            if (sscanf(line, " update_url=%199[^\r\n]", url) == 1) {
                extern void Port_Updater_SetUrlOverride(const char*);
                Port_Updater_SetUrlOverride(url);
                continue;
            }
        }
        if (sscanf(line, " ra_username=%63[^\r\n]", key) == 1) {
            extern void Port_RA_SetUsername(const char*);
            Port_RA_SetUsername(key);
            continue;
        }
        if (sscanf(line, " ra_token=%63[^\r\n]", key) == 1) {
            extern void Port_RA_SetToken(const char*);
            Port_RA_SetToken(key);
            continue;
        }
        if (sscanf(line, " %63[^=]=%d", key, &val) != 2) continue;
        if (strcmp(key, "ra_enabled") == 0) {
            extern void Port_RA_SetEnabled(bool);
            Port_RA_SetEnabled(val != 0);
        } else if (strcmp(key, "ra_hardcore") == 0) {
            extern void Port_RA_SetHardcore(bool);
            /* Restore last session's choice. Port_RA_SetHardcore clamps it
             * back to false when the build forbids hardcore (debug tools). */
            Port_RA_SetHardcore(val != 0);
        } else if (strcmp(key, "ra_sound") == 0) {
            extern void Port_RA_SetNotificationSound(bool);
            Port_RA_SetNotificationSound(val != 0);
        } else if (strcmp(key, "update_auto") == 0) {
            extern void Port_Updater_SetAuto(bool);
            Port_Updater_SetAuto(val != 0);
#ifdef PORT_DEBUG_TOOLS
        } else if (strcmp(key, "debug_ftp") == 0) {
            sDebugFtpOn = val != 0;
#endif
        } else if (strcmp(key, "auto_bomb_jump") == 0) {
            extern void Port_AutoBomb_SetEnabled(bool on);
            Port_AutoBomb_SetEnabled(val != 0);
        } else if (strcmp(key, "update_beta") == 0) {
            extern void Port_Updater_SetBeta(bool);
            Port_Updater_SetBeta(val != 0);
        } else if (strcmp(key, "ra_notify_top") == 0) {
            extern void Port_RA_SetNotifyOnTopScreen(bool);
            Port_RA_SetNotifyOnTopScreen(val != 0);
        } else if (strcmp(key, "language") == 0) {
            if (val >= 0 && val < LANGUAGE_COUNT) {
                extern void SramWrite_Language(void);
                gLanguage = (u8)val;
                SramWrite_Language();
            }
        } else if (strcmp(key, "aspect_ratio") == 0) {
            /* 0 was the old WIDE (a stretched 16:9 variant, retired), so an
             * old file holding it falls through to ORIGINAL; the new WIDE --
             * showing extra world around the GBA frame -- is 3. */
            if (val >= 1 && val <= 3) sAspectRatio = val;
        } else if (strcmp(key, "display_style") == 0) {
            /* BLUR (2) retired -- clamp any old value into PIXEL PERFECT/SCALED. */
            if (val >= 0 && val <= 1) sDisplayStyle = val;
        } else if (strcmp(key, "stereo_depth") == 0 || strcmp(key, "stereo_spread") == 0) {
            /* 3D depth is fixed at BOLD now (the slider covers the rest) --
             * accept and ignore the key for backward compatibility. */
        } else if (strcmp(key, "show_fps") == 0) {
            if (val >= 0 && val <= 4) sFpsPosition = val;
            else sFpsPosition = (val != 0) ? 1 : 0;
        } else if (strcmp(key, "auto_hide_hud") == 0) {
            sAutoHideHud = (val != 0);
        } else if (strcmp(key, "hide_spoilers") == 0) {
            sHideSpoilers = (val != 0);
        } else if (strcmp(key, "hud_outside") == 0) {
            sHudOutside = (val != 0);
        } else if (strcmp(key, "gba_bezel") == 0) {
            sGbaBezel = (val != 0);
        } else if (strcmp(key, "gba_fx_grade") == 0) {
            if (val >= 0 && val < 4) sGbaFxGrade = val;
        } else if (strcmp(key, "gba_fx_grid") == 0) {
            if (val >= 0 && val < 4) sGbaFxGrid = val;
        } else if (strcmp(key, "gba_fx_vignette") == 0) {
            if (val >= 0 && val < 4) sGbaFxVignette = val;
        } else if (strcmp(key, "frame_pacing") == 0) {
            if (val >= 0 && val < 2) sFramePacing = val;
        } else if (strcmp(key, "btn_map_a") == 0) {
            if (val >= 0 && val < BTN_ACTION_COUNT) sBtnRemap[0] = val;
        } else if (strcmp(key, "btn_map_b") == 0) {
            if (val >= 0 && val < BTN_ACTION_COUNT) sBtnRemap[1] = val;
        } else if (strcmp(key, "btn_map_x") == 0) {
            if (val >= 0 && val < BTN_ACTION_COUNT) sBtnRemap[2] = val;
        } else if (strcmp(key, "btn_map_y") == 0) {
            if (val >= 0 && val < BTN_ACTION_COUNT) sBtnRemap[3] = val;
        } else if (strcmp(key, "btn_map_l") == 0) {
            if (val >= 0 && val < BTN_ACTION_COUNT) sBtnRemap[4] = val;
        } else if (strcmp(key, "btn_map_r") == 0) {
            if (val >= 0 && val < BTN_ACTION_COUNT) sBtnRemap[5] = val;
        } else if (strcmp(key, "btn_map_zl") == 0) {
            if (val >= 0 && val < BTN_ACTION_COUNT) sBtnRemap[6] = val;
        } else if (strcmp(key, "btn_map_zr") == 0) {
            if (val >= 0 && val < BTN_ACTION_COUNT) sBtnRemap[7] = val;
        } else if (strcmp(key, "btn_map_start") == 0) {
            if (val >= 0 && val < BTN_ACTION_COUNT) sBtnRemap[8] = val;
        } else if (strcmp(key, "btn_map_select") == 0) {
            if (val >= 0 && val < BTN_ACTION_COUNT) sBtnRemap[9] = val;
        } else if (strcmp(key, "cstick_mode") == 0) {
            if (val >= 0 && val < 4) sCstickMode = val;
        } else if (strcmp(key, "bottom_tab") == 0) {
            if (val >= 0 && val < BOTTOM_TAB_COUNT) Port_BottomUI_SetTab((PortBottomTab)val);
        } else if (strcmp(key, "bottom_zoom") == 0) {
            Port_BottomUI_SetZoom(val);
        } else if (strcmp(key, "bottom_area") == 0) {
            Port_BottomUI_SetViewArea(val);
        } else if (strcmp(key, "bottom_follow") == 0) {
            Port_BottomUI_SetFollowSamus(val != 0);
        }
    }
    fclose(file);
}

int Port_Config_GetAspectRatio(void) { return sAspectRatio; }
#ifdef PORT_DEBUG_TOOLS
bool Port_Config_DebugFtpOn(void) { return sDebugFtpOn; }
void Port_Config_SetDebugFtpOn(bool on) {
    if (sDebugFtpOn == on) return;
    sDebugFtpOn = on;
    Port_Config_Save();
}
#endif

void Port_Config_SetAspectRatio(int ratio) { if (ratio >= 0 && ratio < 4) { sAspectRatio = ratio; Port_Config_Save(); } }

int Port_Config_GetDisplayStyle(void) { return sDisplayStyle; }
void Port_Config_SetDisplayStyle(int style) { if (style >= 0 && style < 3) { sDisplayStyle = style; Port_Config_Save(); } }

bool Port_Config_GetShowFps(void) { return sFpsPosition != 0; }
int Port_Config_GetFpsPosition(void) { return sFpsPosition; }
void Port_Config_SetFpsPosition(int pos) { sFpsPosition = (pos >= 0 && pos <= 4) ? pos : 0; Port_Config_Save(); }
void Port_Config_CycleFpsPosition(void) { sFpsPosition = (sFpsPosition + 1) % 5; Port_Config_Save(); }
void Port_Config_SetShowFps(bool show) { sFpsPosition = show ? (sFpsPosition ? sFpsPosition : 1) : 0; Port_Config_Save(); }

bool Port_Config_GetAutoHideHud(void) { return sAutoHideHud; }
void Port_Config_SetAutoHideHud(bool autoHide) { sAutoHideHud = autoHide; Port_Config_Save(); }

bool Port_Config_GetHideSpoilers(void) { return sHideSpoilers; }
void Port_Config_SetHideSpoilers(bool hide) { sHideSpoilers = hide; Port_Config_Save(); }

bool Port_Config_GetHudOutside(void) { return sHudOutside; }
void Port_Config_SetHudOutside(bool outside) { sHudOutside = outside; Port_Config_Save(); }
void Port_Config_ToggleHudOutside(void) { sHudOutside = !sHudOutside; Port_Config_Save(); }

bool Port_Config_GetGbaBezel(void) { return sGbaBezel; }
void Port_Config_SetGbaBezel(bool bezel) { sGbaBezel = bezel; Port_Config_Save(); }
void Port_Config_ToggleGbaBezel(void) { sGbaBezel = !sGbaBezel; Port_Config_Save(); }

/* GBA screen-effect levels (0..3). Setters clamp and persist, matching the
 * Get/Set pattern of the toggles above. */
int Port_Config_GetGbaFxGrade(void) { return sGbaFxGrade; }
void Port_Config_SetGbaFxGrade(int level) { if (level >= 0 && level < 4) { sGbaFxGrade = level; Port_Config_Save(); } }

int Port_Config_GetGbaFxGrid(void) { return sGbaFxGrid; }
void Port_Config_SetGbaFxGrid(int level) { if (level >= 0 && level < 4) { sGbaFxGrid = level; Port_Config_Save(); } }

int Port_Config_GetGbaFxVignette(void) { return sGbaFxVignette; }
void Port_Config_SetGbaFxVignette(int level) { if (level >= 0 && level < 4) { sGbaFxVignette = level; Port_Config_Save(); } }

/* 0 = ADAPTIVE (default), 1 = LOCKED 30. */
int Port_Config_GetFramePacing(void) { return sFramePacing; }
void Port_Config_SetFramePacing(int mode) { if (mode >= 0 && mode < 2) { sFramePacing = mode; Port_Config_Save(); } }

int Port_Config_GetBtnRemap(int btn) {
    if (btn >= 0 && btn < 10) return sBtnRemap[btn];
    return 0;
}

int Port_Config_GetButtonMapping(int buttonIndex) {
    return Port_Config_GetBtnRemap(buttonIndex);
}

void Port_Config_SetBtnRemap(int btn, int action) {
    if (btn >= 0 && btn < 10 && action >= 0 && action < BTN_ACTION_COUNT) {
        sBtnRemap[btn] = action;
        Port_Config_Save();
    }
}

void Port_Config_SetButtonMapping(int buttonIndex, int action) {
    Port_Config_SetBtnRemap(buttonIndex, action);
}

int Port_Config_GetCstickMode(void) { return sCstickMode; }
void Port_Config_SetCstickMode(int mode) { if (mode >= 0 && mode < 4) { sCstickMode = mode; Port_Config_Save(); } }

const char* Port_Config_GetActionName(int action, int lang) {
    switch (lang) {
        case 0: /* Japanese */
        case 1: /* Hiragana */
            switch (action) {
                case 0: return "NONE";
                case 1: return "RAPID FIRE";
                case 2: return "QUICK MORPH";
                case 3: return "FIRE (B)";
                case 4: return "JUMP (A)";
                case 5: return "MISSILES (SELECT)";
                case 6: return "AIM (L)";
                case 7: return "ARM WEAPON (R)";
                case 8: return "PAUSE (START)";
                default: return "NONE";
            }
        case 3: /* German */
            switch (action) {
                case 0: return "KEINE";
                case 1: return "DAUERFEUER";
                case 2: return "SCHNELLER MORPH";
                case 3: return "SCHIESSEN (B)";
                case 4: return "SPRINGEN (A)";
                case 5: return "RAKETEN (SELECT)";
                case 6: return "ZIELEN (L)";
                case 7: return "WAFFE WAEHLEN (R)";
                case 8: return "PAUSE (START)";
                default: return "KEINE";
            }
        case 4: /* French */
            switch (action) {
                case 0: return "AUCUN";
                case 1: return "TIR RAPIDE";
                case 2: return "MORPHING RAPIDE";
                case 3: return "TIRER (B)";
                case 4: return "SAUTER (A)";
                case 5: return "MISSILES (SELECT)";
                case 6: return "VISER (L)";
                case 7: return "ARMER MISSILE (R)";
                case 8: return "PAUSE (START)";
                default: return "AUCUN";
            }
        case 5: /* Italian */
            switch (action) {
                case 0: return "NESSUNO";
                case 1: return "FUOCO RAPIDO";
                case 2: return "MORFOSFERA RAPIDA";
                case 3: return "SPARA (B)";
                case 4: return "SALTA (A)";
                case 5: return "MISSILI (SELECT)";
                case 6: return "MIRA (L)";
                case 7: return "ARMA MISSILE (R)";
                case 8: return "PAUSA (START)";
                default: return "NESSUNO";
            }
        case 6: /* Spanish */
            switch (action) {
                case 0: return "NINGUNA";
                case 1: return "AUTODISPARO";
                case 2: return "MORFOSFERA RAPIDA";
                case 3: return "DISPARAR (B)";
                case 4: return "SALTAR (A)";
                case 5: return "MISILES (SELECT)";
                case 6: return "APUNTAR (L)";
                case 7: return "ARMAR MISILES (R)";
                case 8: return "PAUSA (START)";
                default: return "NINGUNA";
            }
        default: /* English */
            switch (action) {
                case 0: return "NONE";
                case 1: return "RAPID FIRE";
                case 2: return "QUICK MORPH";
                case 3: return "FIRE (B)";
                case 4: return "JUMP (A)";
                case 5: return "MISSILES (SELECT)";
                case 6: return "AIM (L)";
                case 7: return "ARM WEAPON (R)";
                case 8: return "PAUSE (START)";
                default: return "NONE";
            }
    }
}

/* Quick Morph (platform_3ds_minimal.c) needs to know Samus's current pose
 * "class" to decide which key to simulate and when it's done:
 *  - entering ball form takes KEY_DOWN TWICE (standing -> crouching on the
 *    first gChangedInput edge, crouching -> morphing on the second -- see
 *    samus.c's SamusCrouching), so the caller must keep sending DOWN edges
 *    until the class becomes MORPHED;
 *  - leaving it takes KEY_UP once to start unmorphing (SamusMorphball's
 *    "Check unmorphing" block), but that only lands Samus in CROUCHING
 *    (SamusUnmorphingGfx hard-sets SPOSE_CROUCHING once the animation ends)
 *    -- a second UP edge is needed from there to actually stand up
 *    (SamusCrouching's own KEY_UP check), so the caller must keep sending UP
 *    edges until the class becomes NORMAL, not just until it leaves MORPHED.
 * A previous version only checked "is Samus in the ball family" and fired a
 * single fixed-length KEY_DOWN pulse, which produces just one edge -- Samus
 * would crouch and get stuck, i.e. behave like a plain Down button. */
int Port_Samus_GetPoseClass(void) {
    switch (gSamusData.pose) {
        case SPOSE_MORPHING:
        case SPOSE_MORPH_BALL:
        case SPOSE_ROLLING:
        case SPOSE_UNMORPHING:
        case SPOSE_MORPH_BALL_MIDAIR:
        case SPOSE_MORPH_BALL_ON_ZIPLINE:
        case SPOSE_PULLING_YOURSELF_INTO_A_MORPH_BALL_TUNNEL:
        case SPOSE_GETTING_HURT_IN_MORPH_BALL:
        case SPOSE_GETTING_KNOCKED_BACK_IN_MORPH_BALL:
            return 2; /* morphed (or mid-transition into/out of ball form) */
        case SPOSE_CROUCHING:
        case SPOSE_UNCROUCHING_FROM_CRAWLING:
        case SPOSE_UNCROUCHING_SUITLESS:
            return 1; /* crouching (or standing back up from it) */
        default:
            return 0; /* normal (standing, walking, jumping, shooting, ...) */
    }
}

/* Whether Samus can actually become a morph ball right now. The Quick Morph
 * assist synthesises Down pulses until the pose class reaches MORPHED; if
 * that can never happen it just bobs her crouch for the whole safety window.
 * Suitless Samus has no morph-ball graphics and the pose machine refuses the
 * transition (see the suitType guard in SamusCrouching), so the assist must
 * not start a morph sequence for her. */
int Port_Samus_CanMorph(void) {
    return gEquipment.suitType != SUIT_SUITLESS;
}

/* Per-area, per-tank-type collected counts for the bottom-screen collectibles
 * breakdown (port_bottom_ui_3ds.c's RenderCollectiblesModal). The map's
 * "obtained item" bitmap (gMinimapTilesWithObtainedItems) only says a tile
 * had *something* collected, not what -- BgClipSetItemAsCollected (bg_clip.c)
 * is the thing that actually knows the type, and it logs every pickup (tanks
 * and major items alike) into gItemsCollected[area][0..gNumberOfItemsCollected[area]),
 * so walk that log and tally by type instead. Major items (ITEM_TYPE_ABILITY)
 * are logged too but deliberately not counted here -- this is a tank/ammo
 * breakdown, matching the per-area totals table (sTotalTanksTable) it's
 * compared against. */
void Port_GetAreaItemTypeCounts(int area, int* outEnergy, int* outMissile, int* outSuper, int* outPowerBomb) {
    *outEnergy = 0;
    *outMissile = 0;
    *outSuper = 0;
    *outPowerBomb = 0;
    if (area < 0 || area >= MAX_AMOUNT_OF_AREAS) return;

    int count = gNumberOfItemsCollected[area];
    if (count > MAX_AMOUNT_OF_ITEMS_PER_AREA) count = MAX_AMOUNT_OF_ITEMS_PER_AREA;
    for (int i = 0; i < count; i++) {
        switch (gItemsCollected[area][i].type) {
            case ITEM_TYPE_ENERGY: (*outEnergy)++; break;
            case ITEM_TYPE_MISSILE: (*outMissile)++; break;
            case ITEM_TYPE_SUPER_MISSILE: (*outSuper)++; break;
            case ITEM_TYPE_POWER_BOMB: (*outPowerBomb)++; break;
            default: break;
        }
    }
}


int Port_Config_Get3DSAspectRatio(void) { return sAspectRatio; }
const char* Port_Config_Get3DSAspectRatioName(void) {
    static const char* const names[] = { "ORIGINAL", "ORIGINAL", "STRETCH", "WIDE" };
    return (sAspectRatio >= 0 && sAspectRatio < 4) ? names[sAspectRatio] : "ORIGINAL";
}
void Port_Config_Cycle3DSAspectRatio(void) {
    if (sDisplayStyle == 0) {
        /* PIXEL PERFECT is 1:1, so STRETCH has no meaning there: the choice
         * is the plain frame or the WIDE view around it. */
        sAspectRatio = (sAspectRatio == 3) ? 1 : 3;
    } else {
        /* SCALED: ORIGINAL -> STRETCH -> WIDE -> ORIGINAL. */
        sAspectRatio = (sAspectRatio == 1) ? 2 : ((sAspectRatio == 2) ? 3 : 1);
    }
    Port_Config_Save();
}

int Port_Config_Get3DSDisplayStyle(void) { return sDisplayStyle; }
const char* Port_Config_Get3DSDisplayStyleName(void) {
    static const char* const names[] = { "PIXEL PERFECT", "SCALED", "BLUR" };
    return (sDisplayStyle >= 0 && sDisplayStyle < 3) ? names[sDisplayStyle] : "PIXEL PERFECT";
}
void Port_Config_Cycle3DSDisplayStyle(void) {
    /* Toggle PIXEL PERFECT (0) <-> SCALED (1); BLUR (2) is retired. */
    sDisplayStyle = (sDisplayStyle + 1) % 2;
    Port_Config_Save();
}

double Port_PPU_3DS_CurrentFps(void) { return sCurrentFps; }

/* Which path actually rendered the most recently presented frame -- the
 * top-screen "FPS NN" overlay only ever gets drawn by the CPU path
 * (DrawTopImageStereo), so it can't tell you the GPU path's real cadence;
 * this backs the bottom-screen debug overlay in platform_gpu_3ds.c instead,
 * which is drawn regardless of which path rendered. */
static bool sLastFrameUsedGpu;
bool Port_PPU_3DS_LastFrameUsedGpu(void) { return sLastFrameUsedGpu; }

bool Port_PPU_Init(void) {
    VirtuaPPUMode1GbaMemory memory = { gIoMem, gVram, gBgPltt, gObjPltt, gOamMem };
    virtuappu_mode1_bind_gba_memory(&memory);
    virtuappu_mode1_set_old3ds_profile(!Platform3DS_IsNew3DS());

    if (!PlatformGpu3DS_Init(!Platform3DS_IsNew3DS())) return false;
    sTopBuffer = PlatformGpu3DS_TopBuffer();
    sTopRightBuffer = PlatformGpu3DS_TopRightBuffer();
    sBottomBuffer = PlatformGpu3DS_BottomBuffer(0);
    if (!sTopBuffer || !sBottomBuffer) return false;
    /* Bottom screen stays solid black -- PlatformGpu3DS_EndBottom() still
     * needs a non-NULL buffer to finish the frame (C2D_Flush/C3D_FrameEnd),
     * it just skips re-uploading it every frame since changed=false. */
    memset(sBottomBuffer, 0, 512u * 256u * sizeof(uint32_t));

    for (int i = 0; i < LOGIC_BUFFER_SLOTS; ++i) {
        sLogicTop[i] = (uint32_t*)malloc((size_t)TOP_PITCH * 160u * sizeof(uint32_t));
        if (!sLogicTop[i]) return false;
        memset(sLogicTop[i], 0, (size_t)TOP_PITCH * 160u * sizeof(uint32_t));
        if (sTopRightBuffer) {
            sLogicTopRight[i] = (uint32_t*)malloc((size_t)TOP_PITCH * 160u * sizeof(uint32_t));
            if (!sLogicTopRight[i]) return false;
            memset(sLogicTopRight[i], 0, (size_t)TOP_PITCH * 160u * sizeof(uint32_t));
        }
    }
    virtuappu_mode1_set_output_buffer(sLogicTop[0], TOP_PITCH);
    if (sTopRightBuffer) {
        virtuappu_mode1_set_right_output_buffer(sLogicTopRight[0], TOP_PITCH);
    }

#ifdef PORT_GPU_TILE_RENDERER
    /* Experimental (EXTRA_CFLAGS=-DPORT_GPU_TILE_RENDERER, off by default):
     * offloads mode-0 tile/sprite compositing to the PICA200 instead of the
     * CPU scanline renderer. Falls back to the CPU path per-frame whenever
     * Port_GpuRenderer_CanRenderFrame() says the frame uses something it
     * doesn't support yet (affine BG, blend, windows, mosaic, affine OBJ) --
     * see port_gpu_renderer.c. Init failure here just means every frame
     * takes the CPU fallback, not a hard error. */
    if (Port_GpuRenderer_Init()) {
        Port_GpuRenderer_SetActive(true);
    }
#endif

    sReady = true;
    return true;
}

/* The PORT_VERBOSE_FRAME_LOG checkpoints below run from the per-frame
 * present path, never a boot/hang checkpoint, so redirect to the buffered
 * logger -- see port_debug_log.h on why the unbuffered version's per-call
 * file I/O is a real cost here (confirmed dominating "upload" time
 * misattribution for most of this session --
 * docs/3ds-port-gpu-renderer-status-2026-08-20.md section 16). The GPU and
 * PERF diagnostic blocks call Port_DebugLog_Gpu / _Perf explicitly instead,
 * so they also honour the DEBUG -> HERRAMIENTAS LOG-mode stream filter. */
extern void Port_DebugLogBuffered(const char* msg);
#define Port_DebugLog Port_DebugLogBuffered

/* Called every VBlankIntrWait() on the game-logic thread (port_bios.c). Does
 * the CPU scanline render only and publishes the result -- see the doc
 * comment on sLogicTop above for why. The experimental GPU tile renderer
 * (PORT_GPU_TILE_RENDERER, off by default) composites straight into the GPU
 * pipeline as its render step, so it can't be deferred to a separate
 * buffer/thread the same way; it keeps the old synchronous inline present,
 * same as PORT_PPU_TEST_PATTERN's direct-to-GPU-buffer diagnostic path. */
void Port_PPU_RenderFrame(void) {
    if (!sReady) return;

    /* Confirmed root cause of a reported bottom-screen debug-overlay
     * duplication (2 copies of the same FPS/stat text stacked vertically,
     * seen both in Azahar and on real hardware): src/transfer.c calls
     * VBlankIntrWait() directly, separate from agbmain's own per-frame
     * Halt loop -- VBlankIntrWait -> Port_Bios_Halt -> this function (see
     * port_bios.c), so a VRAM transfer that spans multiple
     * VBlankIntrWait() calls makes this run more than once per real
     * ~16.67ms display refresh. Each call redraws (and re-presents) the
     * bottom-screen overlay from scratch -- on New3DS in particular,
     * PlatformGpu3DS_EndBottom's own "skip redraw" branch only exists for
     * the Old3DS profile, so a re-entrant call there was never skipped.
     * A real display refresh is ~16.67ms apart; anything under half of
     * that (8ms) between two calls cannot be a new real frame, so skip
     * presenting again entirely rather than redrawing (and thus visually
     * duplicating) the same content. This was previously just logged as a
     * "re-entrant?" diagnostic without actually being fixed -- see
     * docs/3ds-port-gpu-renderer-status-2026-08-20.md section 18. */
    {
        static uint64_t sLastPresentMs;
        uint64_t nowMs = osGetTime();
        if (sLastPresentMs != 0 && (nowMs - sLastPresentMs) < 8u) {
#ifdef PORT_DEBUG_TOOLS_ACTIVE
            {
                char msg[64];
                __builtin_snprintf(msg, sizeof(msg), "PRESENT gap=%lums (skipped, re-entrant)",
                                   (unsigned long)(nowMs - sLastPresentMs));
                extern void Port_DebugLog_Gpu(const char* msg);
                Port_DebugLog_Gpu(msg);
            }
#endif
            return;
        }
        sLastPresentMs = nowMs;
    }

    const uint16_t dispcnt = (uint16_t)(gIoMem[0] | (gIoMem[1] << 8));
    const uint8_t gbaMode = (uint8_t)(dispcnt & 7);

    PPUMemory ppu;
    ppu.frame_width = TOP_NATIVE_W;
    ppu.frame_pitch = TOP_PITCH;
    /* virtuappu internal mode: 1 = tiled (GBA mode 0), 2 = affine (GBA
     * modes 1/2) -- see port/ppu/src/virtuappu.c's virtuappu_render_frame.
     * Zero Mission only ever uses GBA modes 0 and 1 (docs/3ds-port-ppu-audit.md). */
    ppu.mode = (gbaMode == 1 || gbaMode == 2) ? 2 : 1;

    float slider3d = PlatformGpu3DS_Get3DSlider();
    virtuappu_mode1_set_3d_slider(slider3d);

#ifdef PORT_GPU_TILE_RENDERER
    const bool useGpuRenderer = Port_GpuRenderer_IsActive() && Port_GpuRenderer_CanRenderFrame();
#else
    const bool useGpuRenderer = false;
#endif

#ifdef PORT_PPU_TEST_PATTERN
    /* Diagnostic: bypass virtuappu entirely and write a known-good pattern
     * (solid alternating 8px-tall horizontal bars) straight into the output
     * buffer. If this still shows up sheared/diagonal on real hardware, the
     * bug is in the GPU presentation pipeline (platform_gpu_3ds.c) or the
     * buffer geometry (TOP_PITCH/width) below, not in virtuappu/mode1.c.
     * Mutually exclusive with the GPU tile renderer -- both are diagnostic/
     * experimental paths, never built together. */
    for (int row = 0; row < 160; ++row) {
        uint32_t color = ((row / 8) % 2 == 0) ? 0xFFFFFFFFu : 0xFF000000u;
        uint32_t* r = sTopBuffer + (size_t)row * TOP_PITCH;
        for (int col = 0; col < TOP_NATIVE_W; ++col) r[col] = color;
    }
    /* Writes sTopBuffer (the real GPU buffer) directly, bypassing the
     * logic-side ping-pong buffers -- keep the old synchronous inline
     * present below for this diagnostic path. */
    const bool forceSyncPresent = true;
#else
    const bool forceSyncPresent = false;
    /* Issue #29: on CPU-rendered frames, replay the per-scanline haze scroll
     * table into the IO regs line by line (the HBlank DMA this port can't
     * emulate). No-op unless a ripple effect is active. The GPU path does
     * its own thing (offscreen BG3 + strip re-blit), so keep it off there. */
    PortHaze_SetCpuPerLine(!useGpuRenderer);
    if (!useGpuRenderer) {
        virtuappu_mode1_set_output_buffer(sLogicTop[sLogicWriteSlot], TOP_PITCH);
        if (sTopRightBuffer) {
            virtuappu_mode1_set_right_output_buffer(sLogicTopRight[sLogicWriteSlot], TOP_PITCH);
        }
        virtuappu_mode1_set_frame_geometry(&ppu);
        virtuappu_mode1_render_frame(&ppu);

        /* Depth-tint debug view is a GPU-renderer feature (planes only exist
         * there). This frame fell back to the CPU rasterizer -- no stereo, no
         * tier colours -- so stamp an unmistakable magenta border to say
         * "this segment has no 3D data; a cutscene depth override changes
         * nothing here". Cheap: the outer 3px ring of the GBA-native buffer. */
        if (Port_GpuRenderer_DepthTintEnabled()) {
            const uint32_t kMark = 0xFFFF00FFu; /* ABGR: opaque magenta */
            uint32_t* bufs[2] = { sLogicTop[sLogicWriteSlot],
                                  sTopRightBuffer ? sLogicTopRight[sLogicWriteSlot] : NULL };
            for (int bi = 0; bi < 2; ++bi) {
                uint32_t* buf = bufs[bi];
                if (!buf) continue;
                for (int y = 0; y < 160; ++y) {
                    uint32_t* row = buf + (size_t)y * TOP_PITCH;
                    if (y < 3 || y >= 157) {
                        for (int x = 0; x < TOP_NATIVE_W; ++x) row[x] = kMark;
                    } else {
                        row[0] = row[1] = row[2] = kMark;
                        row[TOP_NATIVE_W - 3] = row[TOP_NATIVE_W - 2] = row[TOP_NATIVE_W - 1] = kMark;
                    }
                }
            }
        }
    }
#endif

    /* Log the first few frames unconditionally, then only when DISPCNT
     * actually changes (the interesting boot-sequence transitions --
     * force-blank lifting, mode/BG-enable changes) or every ~5s as a
     * liveness heartbeat, capped so the log can't grow unbounded if the
     * game ends up toggling DISPCNT every frame during normal gameplay.
     * Gated behind PORT_VERBOSE_FRAME_LOG -- even throttled, each triggered
     * dump is 8 flushed file writes, a real cost once boot isn't the thing
     * being debugged anymore. */
#ifdef PORT_VERBOSE_FRAME_LOG
    static uint16_t sLastDispcnt = 0xFFFF;
    static unsigned sInGameDumps;
    static unsigned sBootDumps;
    const bool dispcntChanged = dispcnt != sLastDispcnt;
    sLastDispcnt = dispcnt;
    extern uint8_t gSubGameMode1;
    extern uint8_t gMainGameMode;
    const bool isIngame = (gMainGameMode == 4);
    const bool shouldLog = (isIngame && sInGameDumps < 20 && (sPresentFrameCount % 10 == 0 || dispcntChanged)) ||
                           (!isIngame && (sPresentFrameCount < 10 || dispcntChanged) && sBootDumps < 20);
    if (shouldLog) {
        if (isIngame) ++sInGameDumps; else ++sBootDumps;
        char msg[256];
        const uint16_t bg0cnt = (uint16_t)(gIoMem[0x08] | (gIoMem[0x09] << 8));
        const uint16_t bg1cnt = (uint16_t)(gIoMem[0x0A] | (gIoMem[0x0B] << 8));
        const uint16_t bg2cnt = (uint16_t)(gIoMem[0x0C] | (gIoMem[0x0D] << 8));
        const uint16_t bg3cnt = (uint16_t)(gIoMem[0x0E] | (gIoMem[0x0F] << 8));
        const uint16_t winin = (uint16_t)(gIoMem[0x48] | (gIoMem[0x49] << 8));
        const uint16_t winout = (uint16_t)(gIoMem[0x4A] | (gIoMem[0x4B] << 8));
        const uint16_t win1h = (uint16_t)(gIoMem[0x42] | (gIoMem[0x43] << 8));
        const uint16_t win1v = (uint16_t)(gIoMem[0x46] | (gIoMem[0x47] << 8));
        const uint16_t bldcnt = (uint16_t)(gIoMem[0x50] | (gIoMem[0x51] << 8));
        const uint16_t bldalpha = (uint16_t)(gIoMem[0x52] | (gIoMem[0x53] << 8));
        __builtin_snprintf(msg, sizeof(msg),
            "PPU[%u]: mode=%u/%u dispcnt=%04x bg0-3cnt=%04x,%04x,%04x,%04x winin/out=%04x/%04x win1=%04x,%04x bld=%04x/%04x px[80,120]=%08lx",
            sPresentFrameCount, gMainGameMode, gSubGameMode1, dispcnt,
            bg0cnt, bg1cnt, bg2cnt, bg3cnt, winin, winout, win1h, win1v, bldcnt, bldalpha,
            (unsigned long)(forceSyncPresent ? sTopBuffer : sLogicTop[sLogicWriteSlot])[(size_t)80 * TOP_PITCH + 120]);
        Port_DebugLog(msg);
    }
#endif
    ++sPresentFrameCount;
    UpdateFpsWindow();

    if (!useGpuRenderer && !forceSyncPresent) {
        /* Normal (default-build) path: hand the finished CPU render off to
         * the present thread instead of pushing it to the GPU inline here --
         * see the doc comment on sLogicTop/Port_PPU_RenderFrame above. */
        sPublishedSlot = sLogicWriteSlot;
        __atomic_store_n(&sPublishedGeneration, sPublishedGeneration + 1, __ATOMIC_RELEASE);
        sLogicWriteSlot ^= 1u;
        return;
    }

#if defined(PORT_VERBOSE_FRAME_LOG)
    Port_DebugLog("Port_PPU_PresentFrame: before BeginTop");
#endif
    /* This is the sync path's own direct GPU submission, called from the
     * game-logic thread -- must not run concurrently with the present
     * thread's own submission in Port_PPU_GpuPresentPump below. See
     * platform_gpu_3ds.c's sGpuSubmitLock doc comment. */
#ifdef PORT_GPU_TILE_RENDERER
    /* Collect BEFORE C3D_FrameBegin: FrameBegin blocks until the GPU has
     * finished the previous frame, and the collection is pure CPU work, so
     * doing it first lets the two overlap instead of queueing. The exception
     * is a frame that is about to reassign atlas slots -- that one waits for
     * the GPU first, since the previous frame may still be sampling them. */
    if (useGpuRenderer) {
        if (Port_GpuRenderer_CollectNeedsIdleGpu()) {
            PlatformGpu3DS_SubmitLock_Acquire();
            PlatformGpu3DS_BeginTopSceneGpu();
            Port_GpuRenderer_CollectFrame();
        } else {
            Port_GpuRenderer_CollectFrame();
            PlatformGpu3DS_SubmitLock_Acquire();
        }
    } else
#endif
    {
        PlatformGpu3DS_SubmitLock_Acquire();
    }
#ifdef PORT_GPU_TILE_RENDERER
    if (useGpuRenderer) {
        if (PlatformGpu3DS_BeginTopSceneGpu()) {
            Port_GpuRenderer_DrawFrame();
            sLastFrameUsedGpu = true;
        } else {
            /* Frame-begin failed (GPU busy/queue full) -- nothing was drawn
             * this frame; EndBottom below no-ops on !sFrameActive, so this
             * frame is silently skipped rather than shown corrupt. */
            sLastFrameUsedGpu = false;
        }
    } else
#endif
    {
        PlatformGpu3DS_BeginTopStereo(sTopBuffer, sTopRightBuffer, TOP_NATIVE_W);
#ifdef PORT_GPU_TILE_RENDERER
        sLastFrameUsedGpu = false;
#endif
    }
#if defined(PORT_VERBOSE_FRAME_LOG)
    Port_DebugLog("Port_PPU_PresentFrame: before EndBottom");
#endif
    PlatformGpu3DS_EndBottom(sBottomBuffer, true);
    PlatformGpu3DS_SubmitLock_Release();
#if defined(PORT_VERBOSE_FRAME_LOG)
    Port_DebugLog("Port_PPU_PresentFrame: done");
#endif

#ifdef PORT_DEBUG_TOOLS_ACTIVE
    /* Every ~2s (at the target 60Hz): where is frame time actually going?
     * mode1's main/worker ticks cover the CPU scanline render (per-thread,
     * so on New3DS 3 threads run it in parallel -- main plus 2 workers, one
     * of which shares Core 1 with the audio thread); GPU drawing/processing
     * times come from citro3d's own counters and cover texture upload +
     * compositing on the PICA200. Adding all three CPU-side numbers doesn't
     * give total frame time (they overlap across threads); compare each one
     * individually against the 16.67ms/frame budget for 60 FPS. */
    if ((sPresentFrameCount % 60u) == 0u) {
        VirtuaPPUMode13DSStats mode1Stats;
        virtuappu_mode1_get_3ds_stats(&mode1Stats);
        PlatformGpu3DSStats gpuStats;
        PlatformGpu3DS_GetStats(&gpuStats);
        extern unsigned PortGbaScreenFx_DebugLastSubmitUs(void);
        char msg[240];
        __builtin_snprintf(msg, sizeof(msg),
            "PERF[%u]: fps=%.1f main=%.2fms w0=%.2fms w1=%.2fms(workers=%lu) gpuDraw=%.2fms gpuProc=%.2fms gbaFxSubmit=%uus",
            sPresentFrameCount, sCurrentFps,
            (double)mode1Stats.mainLastTicks / PORT_PPU_PERF_CPU_TICKS_PER_MSEC,
            (double)mode1Stats.workerLastTicks[0] / PORT_PPU_PERF_CPU_TICKS_PER_MSEC,
            (double)mode1Stats.workerLastTicks[1] / PORT_PPU_PERF_CPU_TICKS_PER_MSEC,
            (unsigned long)mode1Stats.workerCount,
            (double)gpuStats.drawingTime, (double)gpuStats.processingTime,
            PortGbaScreenFx_DebugLastSubmitUs());
        extern void Port_DebugLog_Perf(const char* msg);
        Port_DebugLog_Perf(msg);
    }
#endif
}

/* Present thread (main_3ds.c) entry point: consumes the latest frame
 * Port_PPU_RenderFrame() published (if any) and does the actual GPU
 * submission -- the part that can legitimately block/stall on a busy or
 * buggy GPU pipeline (still-unresolved bottom-screen duplication included)
 * without that stall reaching back into game logic or audio production.
 * Returns false when there was nothing new to present, so the caller can
 * fall back to waiting for the next real display refresh instead of
 * spinning. Not reentrant-guarded like the old inline path needed to be --
 * a generation counter can't be observed twice, so there is nothing to
 * de-duplicate here. */
bool Port_PPU_GpuPresentPump(void) {
    if (!sReady) return false;
    const unsigned generation = __atomic_load_n(&sPublishedGeneration, __ATOMIC_ACQUIRE);
    if (generation == sConsumedGeneration) return false;
    sConsumedGeneration = generation;
    const unsigned slot = sPublishedSlot;

    memcpy(sTopBuffer, sLogicTop[slot], (size_t)TOP_PITCH * 160u * sizeof(uint32_t));
    if (sTopRightBuffer) {
        memcpy(sTopRightBuffer, sLogicTopRight[slot], (size_t)TOP_PITCH * 160u * sizeof(uint32_t));
    }

#ifdef PORT_GPU_TILE_RENDERER
    sLastFrameUsedGpu = false;
#endif
    /* See platform_gpu_3ds.c's sGpuSubmitLock doc comment -- the game-logic
     * thread can still be mid-submission via the PORT_GPU_TILE_RENDERER/
     * PORT_PPU_TEST_PATTERN sync path in Port_PPU_RenderFrame above. */
    PlatformGpu3DS_SubmitLock_Acquire();
    PlatformGpu3DS_BeginTopStereo(sTopBuffer, sTopRightBuffer, TOP_NATIVE_W);
    PlatformGpu3DS_EndBottom(sBottomBuffer, true);
    PlatformGpu3DS_SubmitLock_Release();
    return true;
}

/**
 * Debug dump of Samus's animation/graphics state, for tracking down bugs
 * like #17 (wrong sprite/palette during the death animation) where the
 * VRAM/OAM/palette dump alone doesn't say which pose/frame/suit produced it.
 * Written together with the VRAM/OAM/palette screen dump (debug tools menu).
 */
void PortPpuMzm_DumpSamusState(void) {
    /* Same rotating slot as the rest of this screen dump -- see
     * PlatformGpu3DS_DumpScreens. Declared locally rather than pulled in
     * from platform_gpu_3ds.h, which this file cannot include (its u32
     * typedef from <3ds.h> conflicts with the GBA-port one that
     * structs/samus.h brings in here). */
    extern unsigned PlatformGpu3DS_DumpSetIndex(void);
    const unsigned set = PlatformGpu3DS_DumpSetIndex();
    char dumpPath[256];
    snprintf(dumpPath, sizeof(dumpPath), PORT_DEBUG_DIR "/mzm-dump-%02u-samus.txt", set);
    FILE* f = fopen(dumpPath, "w");
    if (!f)
        return;

    fprintf(f, "pose=%u\n", (unsigned)gSamusData.pose);
    fprintf(f, "currentAnimationFrame=%u\n", (unsigned)gSamusData.currentAnimationFrame);
    fprintf(f, "walljumpTimer=%u\n", (unsigned)gSamusData.walljumpTimer);
    fprintf(f, "suitType=%u\n", (unsigned)gEquipment.suitType);
    fprintf(f, "suitMiscActivation=%u\n", (unsigned)gEquipment.suitMiscActivation);
    fprintf(f, "shoulderGfxSize=%u\n", (unsigned)gSamusPhysics.shoulderGfxSize);
    fprintf(f, "torsoGfxSize=%u\n", (unsigned)gSamusPhysics.torsoGfxSize);
    fprintf(f, "legsGfxSize=%u\n", (unsigned)gSamusPhysics.legsGfxSize);
    fprintf(f, "bodyLowerHalfGfxSize=%u\n", (unsigned)gSamusPhysics.bodyLowerHalfGfxSize);
    fprintf(f, "armCannonGfxUpperSize=%u\n", (unsigned)gSamusPhysics.armCannonGfxUpperSize);
    fprintf(f, "armCannonGfxLowerSize=%u\n", (unsigned)gSamusPhysics.armCannonGfxLowerSize);
    fprintf(f, "unk_22=%u\n", (unsigned)gSamusPhysics.unk_22);

    fclose(f);

    snprintf(dumpPath, sizeof(dumpPath), PORT_DEBUG_DIR "/mzm-dump-%02u-samusdata.bin", set);
    FILE* fb = fopen(dumpPath, "wb");
    if (fb) {
        fwrite(&gSamusData, 1, sizeof(gSamusData), fb);
        fclose(fb);
    }

    snprintf(dumpPath, sizeof(dumpPath), PORT_DEBUG_DIR "/mzm-dump-%02u-samusphysics.bin", set);
    fb = fopen(dumpPath, "wb");
    if (fb) {
        fwrite(&gSamusPhysics, 1, sizeof(gSamusPhysics), fb);
        fclose(fb);
    }

    /* Sprite + WIDE state: enough to tell apart "the game never put the
     * enemy in OAM" (status/onscreen bits, culling margins) from "it is in
     * OAM but the renderer placed it somewhere else" (per-slot WIDE origin
     * tags, which undo the OAM Y/X wrap). */
    snprintf(dumpPath, sizeof(dumpPath), PORT_DEBUG_DIR "/mzm-dump-%02u-sprites.txt", set);
    f = fopen(dumpPath, "w");
    if (!f)
        return;

    extern bool PortWide_FrameDrawn(void);
    extern bool PortWide_GameActive(void);
    extern int Port_WideMarginSubPixelX(void);
    extern int Port_WideMarginSubPixelY(void);
    extern bool PortWide_SlotOrigin(int oamIndex, int* outY, int* outX);
    extern bool Port_Bios_AdaptiveFrameSkipEnabled(void);
    fprintf(f, "bg1=%u,%u wideActive=%d frameDrawn=%d marginSub=%d,%d frameSkip=%d new3ds=%d\n",
            (unsigned)gBg1XPosition, (unsigned)gBg1YPosition, (int)PortWide_GameActive(),
            (int)PortWide_FrameDrawn(), Port_WideMarginSubPixelX(), Port_WideMarginSubPixelY(),
            (int)Port_Bios_AdaptiveFrameSkipEnabled(), (int)Platform3DS_IsNew3DS());
    for (int i = 0; i < MAX_AMOUNT_OF_SPRITES; ++i) {
        const struct SpriteData* s = &gSpriteData[i];
        if (!(s->status & SPRITE_STATUS_EXISTS))
            continue;
        fprintf(f, "spr%02d id=%02x status=%04x props=%02x pos=%u,%u draw=%u/%u/%u order=%u pose=%02x\n",
                i, s->spriteId, (unsigned)s->status, (unsigned)s->properties,
                (unsigned)s->xPosition, (unsigned)s->yPosition, s->drawDistanceTop,
                s->drawDistanceBottom, s->drawDistanceHorizontal, s->drawOrder, s->pose);
    }
    for (int slot = 0; slot < 128; ++slot) {
        int originY, originX;
        if (PortWide_SlotOrigin(slot, &originY, &originX))
            fprintf(f, "tag%03d origin=%d,%d\n", slot, originX, originY);
    }
    fclose(f);
}

/**
 * Debug instant-kill, for iterating on issue #17 (death animation) without
 * having to actually get hit down to 0 energy in real gameplay every time.
 * Mirrors exactly what lethal damage does in src/sprite_util.c's
 * SpriteUtilTakeDamageFromSprite (the `else` branch: zero energy, then
 * request the hurt pose) rather than poking gSamusData.pose directly --
 * SamusSetPose(SPOSE_HURT_REQUEST) is the real entry point that backs up
 * Samus's data into gSamusDataCopy and calls SamusChangeToHurtPose, which
 * itself checks gEquipment.currentEnergy and only then transitions to
 * SPOSE_DYING (src/samus.c). Zeroing energy first reproduces that exact
 * path instead of a synthetic one. Triggered from the debug tools menu
 * (port_bottom_ui_3ds.c, which can't call SamusSetPose directly for the
 * same <3ds.h>/structs-samus.h conflict reason PortPpuMzm_GetSamusRecordState
 * exists). No-op if Samus is already in a hurt/dying/getting-knocked-back
 * pose so pressing it again doesn't
 * re-trigger mid-animation.
 */
void PortPpuMzm_DebugKillSamus(void) {
    switch (gSamusData.pose) {
        case SPOSE_GETTING_HURT:
        case SPOSE_GETTING_HURT_IN_MORPH_BALL:
        case SPOSE_GETTING_KNOCKED_BACK:
        case SPOSE_GETTING_KNOCKED_BACK_IN_MORPH_BALL:
        case SPOSE_DYING:
            return;
        default:
            break;
    }
    gEquipment.currentEnergy = 0;
    SamusSetPose(SPOSE_HURT_REQUEST);
}

/**
 * Compact Samus state for the scene recorder (platform_gpu_3ds.c):
 * that file can't include structs/samus.h directly (its u32 typedef conflicts
 * with <3ds.h>'s, see PortPpuMzm_DumpSamusState's comment above), so it gets
 * the handful of fields worth recording per sample through here instead.
 * `out` must have space for PORT_PPU_MZM_RECORD_STATE_WORDS u32s.
 */
#define PORT_PPU_MZM_RECORD_STATE_WORDS 6
void PortPpuMzm_GetSamusRecordState(uint32_t* out) {
    out[0] = (uint32_t)gSamusData.pose;
    out[1] = (uint32_t)gSamusData.currentAnimationFrame;
    out[2] = (uint32_t)gSamusData.walljumpTimer;
    out[3] = (uint32_t)gEquipment.suitType;
    out[4] = (uint32_t)gEquipment.suitMiscActivation;
    out[5] = (uint32_t)gSamusPhysics.unk_22;
}

/* ---------------------------------------------------------------------
 * Debug warp ("teletransporte")
 *
 * Lives here rather than in port_bottom_ui_3ds.c (which owns the menu that
 * triggers it) for the same reason PortPpuMzm_DebugKillSamus does: that
 * file includes <3ds.h>, whose u32 typedef conflicts with the GBA port's
 * own, so it can't touch gSamusData/gCurrentArea/the door tables directly.
 *
 * Warps are addressed by (area, DOOR id) rather than by (area, room) plus
 * raw coordinates. src/room.c's RoomReset derives EVERYTHING from
 * gLastDoorUsed: gCurrentRoom = pDoor->sourceRoom, and Samus's position
 * from the door's xStart/yEnd/xExit/yExit -- exactly what a normal door
 * transition does. Setting a room number and a guessed x/y instead (what
 * the PORT_LINUX_DIAG_WARP_TO_DEOREM block in src/agbmain.c does, where the
 * coordinates were hand-tuned for one specific room) would drop Samus
 * wherever those coordinates happen to land in the new room's geometry --
 * fine for one known room, useless as a general "jump anywhere" tool.
 * RoomReset recomputes gCurrentRoom from the door regardless, so the room
 * number here is only ever advisory/for display.
 *
 * The point is persisted to sdmc:/3ds/mzm-warp-point.txt so it survives a
 * reflash: the whole reason this exists is iterating on a rendering bug
 * that only shows up in one specific room, where re-walking there after
 * every new CIA install is the actual cost.
 * ------------------------------------------------------------------- */
#define PORT_WARP_POINT_PATH PORT_STATES_DIR "/mzm-warp-point.txt"

/* Populated by RoomInitDoors (src/room.c). Same extern src/menus/boot_debug.c
 * uses for the original game's own room/door debug menu. */
extern const struct Door* sAreaDoorsPointers[AREA_ENTRY_COUNT];

/* Safety cap for the door-table walk below. No real area comes close, but
 * the table is terminated by a DOOR_TYPE_NONE entry rather than by a count,
 * so an unexpected table must not turn into an unbounded scan. */
#define PORT_WARP_MAX_DOORS 128

static bool sWarpPointValid;
static bool sWarpPending;
static u8 sWarpArea;
static u8 sWarpDoor;
static bool sWarpLoadTried;

/* Selection shown in the menu's manual AREA/DOOR spinner. Starts on
 * Crateria rather than area 0 purely because that's where the room this
 * tool was first needed for lives. */
static u8 sWarpSelArea = AREA_CRATERIA;
static u8 sWarpSelDoor = 0;

int PortPpuMzm_DebugGetDoorCount(int area) {
    if (area < 0 || area >= AREA_ENTRY_COUNT) return 0;
    const struct Door* doors = sAreaDoorsPointers[area];
    if (doors == NULL) return 0;
    int n = 0;
    while (n < PORT_WARP_MAX_DOORS && doors[n].type != DOOR_TYPE_NONE) ++n;
    return n;
}

/* Room a given door leads into, or -1 if the door id is out of range. Lets
 * the menu show "PUERTA 12 -> SALA 8", so a room can be found by stepping
 * doors without knowing door ids up front. */
int PortPpuMzm_DebugGetDoorRoom(int area, int door) {
    if (door < 0 || door >= PortPpuMzm_DebugGetDoorCount(area)) return -1;
    return (int)sAreaDoorsPointers[area][door].sourceRoom;
}

static void PortPpuMzm_WarpPointLoad(void) {
    sWarpLoadTried = true;
    FILE* f = fopen(PORT_WARP_POINT_PATH, "r");
    if (!f) return;
    unsigned a = 0, d = 0;
    if (fscanf(f, "%u %u", &a, &d) == 2 && a < AREA_ENTRY_COUNT) {
        sWarpArea = (u8)a;
        sWarpDoor = (u8)d;
        sWarpPointValid = true;
        sWarpSelArea = sWarpArea;
        sWarpSelDoor = sWarpDoor;
    }
    fclose(f);
}

/* Records the door Samus last came through as the warp target, i.e.
 * "bring me back to this room". */
void PortPpuMzm_DebugSaveWarpPoint(void) {
    sWarpArea = (u8)gCurrentArea;
    sWarpDoor = gLastDoorUsed;
    sWarpPointValid = true;
    sWarpLoadTried = true;
    sWarpSelArea = sWarpArea;
    sWarpSelDoor = sWarpDoor;

    FILE* f = fopen(PORT_WARP_POINT_PATH, "w");
    if (f) {
        fprintf(f, "%u %u\n", sWarpArea, sWarpDoor);
        fclose(f);
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "WARP POINT SAVED: area=%u door=%u room=%u",
             sWarpArea, sWarpDoor, gCurrentRoom);
    Port_DebugLog(msg);
}

bool PortPpuMzm_DebugHasWarpPoint(void) {
    if (!sWarpPointValid && !sWarpLoadTried) PortPpuMzm_WarpPointLoad();
    return sWarpPointValid;
}

void PortPpuMzm_DebugGetWarpPointInfo(char* out, int outSize) {
    if (!out || outSize <= 0) return;
    if (!PortPpuMzm_DebugHasWarpPoint()) {
        snprintf(out, (size_t)outSize, "SIN PUNTO GUARDADO");
        return;
    }
    int room = PortPpuMzm_DebugGetDoorRoom(sWarpArea, sWarpDoor);
    snprintf(out, (size_t)outSize, "AREA %u PUERTA %u -> SALA %d", sWarpArea, sWarpDoor, room);
}

/* Manual (area, door) spinner state, driven by the menu's arrows. */
void PortPpuMzm_DebugGetWarpSelection(int* outArea, int* outDoor, int* outRoom) {
    if (!sWarpLoadTried) PortPpuMzm_WarpPointLoad();
    if (outArea) *outArea = sWarpSelArea;
    if (outDoor) *outDoor = sWarpSelDoor;
    if (outRoom) *outRoom = PortPpuMzm_DebugGetDoorRoom(sWarpSelArea, sWarpSelDoor);
}

void PortPpuMzm_DebugStepWarpArea(int delta) {
    int a = (int)sWarpSelArea + delta;
    if (a < 0) a = AREA_ENTRY_COUNT - 1;
    if (a >= AREA_ENTRY_COUNT) a = 0;
    sWarpSelArea = (u8)a;
    /* A door id valid in the previous area usually isn't in the new one. */
    if (sWarpSelDoor >= PortPpuMzm_DebugGetDoorCount(sWarpSelArea)) sWarpSelDoor = 0;
}

void PortPpuMzm_DebugStepWarpDoor(int delta) {
    int count = PortPpuMzm_DebugGetDoorCount(sWarpSelArea);
    if (count <= 0) { sWarpSelDoor = 0; return; }
    int d = (int)sWarpSelDoor + delta;
    if (d < 0) d = count - 1;
    if (d >= count) d = 0;
    sWarpSelDoor = (u8)d;
}

/* Requests a warp; the jump itself happens between frames (Port_SaveState_ServicePending, called from src/agbmain.c's loop). */
static bool PortPpuMzm_RequestWarpTo(u8 area, u8 door) {
    if (PortPpuMzm_DebugGetDoorRoom(area, door) < 0) return false;
    sWarpArea = area;
    sWarpDoor = door;
    sWarpPending = true;
    return true;
}

bool PortPpuMzm_DebugRequestWarp(void) {
    if (!PortPpuMzm_DebugHasWarpPoint()) return false;
    return PortPpuMzm_RequestWarpTo(sWarpArea, sWarpDoor);
}

bool PortPpuMzm_DebugRequestWarpToSelection(void) {
    return PortPpuMzm_RequestWarpTo(sWarpSelArea, sWarpSelDoor);
}

/* Called once per main-loop iteration (from Port_SaveState_ServicePending). Only fires while
 * really in gameplay (GM_INGAME / SUB_GAME_MODE_PLAYING): applying it during
 * a menu, a cutscene or a door transition would fight whatever state machine
 * owns gSubGameMode1 at that moment. The request stays pending until
 * gameplay is reached, so triggering it from the pause screen warps as soon
 * as the game resumes instead of being dropped. */
void PortPpuMzm_DebugApplyPendingWarp(void) {
    if (!sWarpPending) return;
    if (gMainGameMode != GM_INGAME || gSubGameMode1 != SUB_GAME_MODE_PLAYING) return;

    gCurrentArea = (Area)sWarpArea;
    gLastDoorUsed = sWarpDoor;
    /* Advisory only -- RoomReset overwrites this from the door -- but it
     * keeps anything reading gCurrentRoom before then consistent. */
    gCurrentRoom = sAreaDoorsPointers[sWarpArea][sWarpDoor].sourceRoom;
    gSubGameMode1 = 0; /* re-enter InitAndLoadGenerics -> real RoomLoad */
    sWarpPending = false;

    char msg[96];
    snprintf(msg, sizeof(msg), "WARP APPLIED: area=%u door=%u room=%u",
             sWarpArea, sWarpDoor, gCurrentRoom);
    Port_DebugLog(msg);
}


/* ---------------------------------------------------------------------
 * Debug: warp to a minimap tile, reveal every map, edit equipment.
 *
 * Same placement rationale as the warp block above: everything here needs
 * the GBA-side structs that port_bottom_ui_3ds.c (which owns the menus)
 * cannot include alongside <3ds.h>.
 * ------------------------------------------------------------------- */

/* Room table, populated by RoomInitRoomEntries (src/room.c). Needed to turn
 * a door into the minimap tile it sits on. */
extern const struct RoomEntryRom* sAreaRoomEntryPointers[AREA_ENTRY_COUNT];

/* Minimap tile a door occupies. Same arithmetic as
 * BootDebugUpdateMapScreenPosition (src/menus/boot_debug.c), which is the
 * game's own "where on the area map is this door" code: the room's map
 * origin plus the door's offset within the room, in screens. The constant
 * 2-block bias is that function's `xOffset`/`yOffset` default -- doors sit
 * two blocks inside the screen edge. */
static bool PortPpuMzm_DoorMapTile(int area, int door, int* outX, int* outY) {
    if (PortPpuMzm_DebugGetDoorRoom(area, door) < 0) return false;
    const struct Door* pDoor = &sAreaDoorsPointers[area][door];
    const struct RoomEntryRom* pRoom = &sAreaRoomEntryPointers[area][pDoor->sourceRoom];
    *outX = (int)pRoom->mapX + ((int)pDoor->xStart - 2) / SCREEN_SIZE_X_BLOCKS;
    *outY = (int)pRoom->mapY + ((int)pDoor->yStart - 2) / SCREEN_SIZE_Y_BLOCKS;
    return true;
}

/* Door whose map tile is nearest to (tileX, tileY), or -1 if the area has
 * none within `maxDist` tiles. Nearest-match rather than exact because a
 * room can span several map tiles while its doors only sit on the tiles at
 * its edges -- tapping the middle of a big room should still work. */
int PortPpuMzm_DebugFindDoorNearMapTile(int area, int tileX, int tileY, int maxDist) {
    int best = -1;
    int bestDist = maxDist * maxDist + 1;
    int count = PortPpuMzm_DebugGetDoorCount(area);
    for (int d = 0; d < count; ++d) {
        int dx, dy;
        if (!PortPpuMzm_DoorMapTile(area, d, &dx, &dy)) continue;
        int ddx = dx - tileX;
        int ddy = dy - tileY;
        int dist = ddx * ddx + ddy * ddy;
        if (dist < bestDist) {
            bestDist = dist;
            best = d;
        }
    }
    return best;
}

/* Warps to whatever door is nearest the given map tile. Returns the door id
 * used, or -1 when the area has nothing close enough. */
int PortPpuMzm_DebugRequestWarpToMapTile(int area, int tileX, int tileY) {
    int door = PortPpuMzm_DebugFindDoorNearMapTile(area, tileX, tileY, 6);
    if (door < 0) return -1;
    if (!PortPpuMzm_RequestWarpTo((u8)area, (u8)door)) return -1;
    char msg[96];
    snprintf(msg, sizeof(msg), "MAP WARP: area=%d tile=%d,%d -> door=%d", area, tileX, tileY, door);
    Port_DebugLog(msg);
    return door;
}

/* Marks every tile of every area as explored, and every area map as
 * downloaded.
 *
 * gVisitedMinimapTiles is the persistent per-area bitfield (one bit per
 * column, 32 rows) the save file carries; setting it wholesale is what
 * "explored" means. downloadedMapStatus is the separate per-area
 * map-station bit that MinimapSetDownloadedTiles checks.
 *
 * The current area additionally keeps a decompressed working copy
 * (gDecompressedMinimapVisitedTiles) that is only rebuilt on an area
 * transition, so it has to be refreshed by hand here -- with exactly the
 * sequence MinimapCheckOnTransition uses (copy the raw map over it, then
 * MinimapSetDownloadedTiles), or the tiles the player is standing among
 * would stay dark until they walked to another area and back. */
/* Rebuild the current area's rendered tilemap from the base + the download
 * + visited pass. The memcpy is essential: MinimapSetDownloadedTiles mutates
 * its buffer in place (strips the "hidden" flag, adds the 0x1000 explored
 * bit), so it has to start from the clean base each time or a second call is
 * a no-op and nothing new appears. */
static void MinimapRefreshCurrent(void) {
    memcpy(gDecompressedMinimapVisitedTiles, gDecompressedMinimapData,
           MINIMAP_SIZE * MINIMAP_SIZE * sizeof(u16));
    MinimapSetDownloadedTiles(gCurrentArea, gDecompressedMinimapVisitedTiles);
}

/* Fill `buf` with an area's minimap as it renders once downloaded: base
 * decompress + item/boss icons + the downloaded-tiles pass. After this any
 * cell != MINIMAP_TILE_BACKGROUND is a drawn map tile (pause_screen_map.c
 * uses the same test). Forces the downloaded bit for the duration. */
static void MinimapAreaResolved(int area, u16* buf) {
    u8 saved = gEquipment.downloadedMapStatus;
    gEquipment.downloadedMapStatus |= (u8)(1u << area);
    if (area == gCurrentArea)
        memcpy(buf, gDecompressedMinimapData, MINIMAP_SIZE * MINIMAP_SIZE * sizeof(u16));
    else
        PauseScreenGetMinimapData((Area)area, buf);
    MinimapSetDownloadedTiles((Area)area, buf);
    gEquipment.downloadedMapStatus = saved;
}

/* Snapshot of an area's real exploration, taken the first time a debug
 * reveal/mark touches it, so "clear" (third tap) restores exactly what the
 * player had rather than wiping it. */
static u32  sMapBackup[MAX_AMOUNT_OF_AREAS][MINIMAP_SIZE];
static u8   sMapBackupDl[MAX_AMOUNT_OF_AREAS];
static bool sMapBackedUp[MAX_AMOUNT_OF_AREAS];

static void MinimapBackupArea(int area) {
    if (sMapBackedUp[area]) return;
    for (int r = 0; r < MINIMAP_SIZE; ++r)
        sMapBackup[area][r] = gVisitedMinimapTiles[area][r];
    sMapBackupDl[area] = (u8)((gEquipment.downloadedMapStatus >> area) & 1u);
    sMapBackedUp[area] = true;
}

/* Just the per-area "map downloaded" bit -- like touching a map station:
 * the area outline appears, only tiles Samus actually walked read as
 * explored (no whole-area green fill). */
void PortPpuMzm_DebugRevealAllMaps(void) {
    gEquipment.downloadedMapStatus = 0xFF;
    MinimapRefreshCurrent();
    Port_DebugLog("DEBUG: all maps revealed");
}

void PortPpuMzm_DebugRevealAreaMap(int area) {
    if (area < 0 || area >= MAX_AMOUNT_OF_AREAS) return;
    MinimapBackupArea(area);
    gEquipment.downloadedMapStatus |= (u8)(1u << area);
    if (area == gCurrentArea) MinimapRefreshCurrent();
    Port_DebugLog("DEBUG: area map revealed");
}

/* ---- Minimap completion --------------------------------------------- *
 * "Drawn map tile" = != MINIMAP_TILE_BACKGROUND in the tilemap after the
 * download pass (pause_screen_map.c uses that exact test). The base has
 * room tiles spread across several id ranges plus 0xF000-flag bits, so a
 * plain id range misses most of them -- resolve first, then count. */
static int sMapTileTotal[MAX_AMOUNT_OF_AREAS] = { [0 ... MAX_AMOUNT_OF_AREAS - 1] = -1 };

static int MinimapAreaDrawnCount(int area, u16* resolved /*optional, 1024 u16*/) {
    static u16 local[MINIMAP_SIZE * MINIMAP_SIZE];
    u16* buf = resolved ? resolved : local;
    MinimapAreaResolved(area, buf);
    int n = 0;
    for (int i = 0; i < MINIMAP_SIZE * MINIMAP_SIZE; ++i)
        if (buf[i] != MINIMAP_TILE_BACKGROUND) ++n;
    return n;
}

void PortPpuMzm_DebugGetAreaMapProgress(int area, int* outVisited, int* outTotal) {
    if (outVisited) *outVisited = 0;
    if (outTotal) *outTotal = 0;
    if (area < 0 || area >= MAX_AMOUNT_OF_AREAS) return;

    if (sMapTileTotal[area] < 0)
        sMapTileTotal[area] = MinimapAreaDrawnCount(area, NULL);
    int total = sMapTileTotal[area];

    int vis = 0;
    for (int r = 0; r < MINIMAP_SIZE; ++r)
        vis += __builtin_popcount(gVisitedMinimapTiles[area][r]);
    if (vis > total) vis = total;

    if (outVisited) *outVisited = vis;
    if (outTotal) *outTotal = total;
}

int PortPpuMzm_DebugAreaMapPercent(int area) {
    int v = 0, t = 0;
    PortPpuMzm_DebugGetAreaMapProgress(area, &v, &t);
    return t > 0 ? (v * 100 + t / 2) / t : 0;
}

/* STATUS second tap: mark every drawn tile of the (downloaded) area
 * visited, so the whole outline fills in and the % reads 100. */
void PortPpuMzm_DebugMarkAreaVisited(int area) {
    if (area < 0 || area >= MAX_AMOUNT_OF_AREAS) return;
    MinimapBackupArea(area);
    gEquipment.downloadedMapStatus |= (u8)(1u << area);

    static u16 buf[MINIMAP_SIZE * MINIMAP_SIZE];
    int drawn = MinimapAreaDrawnCount(area, buf);
    for (int r = 0; r < MINIMAP_SIZE; ++r)
        for (int c = 0; c < MINIMAP_SIZE; ++c)
            if (buf[r * MINIMAP_SIZE + c] != MINIMAP_TILE_BACKGROUND)
                gVisitedMinimapTiles[area][r] |= (1u << c);
    sMapTileTotal[area] = drawn; /* keep total and marked set in lockstep */
    if (area == gCurrentArea) MinimapRefreshCurrent();
    Port_DebugLog("DEBUG: area marked fully visited");
}

/* STATUS first tap of a new cycle: restore the area to exactly what the
 * player had before the debug reveal/mark (from MinimapBackupArea's
 * snapshot). If there is no snapshot, fall back to a full clear. */
void PortPpuMzm_DebugClearAreaMap(int area) {
    if (area < 0 || area >= MAX_AMOUNT_OF_AREAS) return;
    if (sMapBackedUp[area]) {
        for (int r = 0; r < MINIMAP_SIZE; ++r)
            gVisitedMinimapTiles[area][r] = sMapBackup[area][r];
        if (sMapBackupDl[area]) gEquipment.downloadedMapStatus |= (u8)(1u << area);
        else                    gEquipment.downloadedMapStatus &= (u8)~(1u << area);
        sMapBackedUp[area] = false; /* re-snapshot on the next debug touch */
    } else {
        for (int r = 0; r < MINIMAP_SIZE; ++r)
            gVisitedMinimapTiles[area][r] = 0;
        gEquipment.downloadedMapStatus &= (u8)~(1u << area);
    }
    if (area == gCurrentArea) MinimapRefreshCurrent();
    Port_DebugLog("DEBUG: area map restored");
}

/* ---- Cheat tick ----------------------------------------------------------
 * God mode: forced from Port_BottomUI_FrameTick once per frame. While on,
 * energy is held at max (invincible) and ammo counts are kept topped
 * (infinite). Deliberately does NOT raise beam damage -- that would let
 * every shot break blocks; a "one-shot bosses without breaking anything"
 * cheat is a separate, later thing. */
static bool sDebugGod = false;
void PortPpuMzm_DebugSetGod(bool on) { sDebugGod = on; }
bool PortPpuMzm_DebugGetGod(void) { return sDebugGod; }

/* "Always active" forced continuous effects, keyed by SMF_* bit. Only
 * SMF_SPEEDBOOSTER and SMF_SCREW_ATTACK are meaningful here. Independent of
 * god mode: driven every frame from PortPpuMzm_DebugCheatTick. */
static u8 sForcedEffects = 0;

void PortPpuMzm_DebugSetForceEffect(unsigned smfBit, bool on) {
    if (on) {
        sForcedEffects |= (u8)smfBit;
        gEquipment.suitMisc |= (u8)smfBit;
        gEquipment.suitMiscActivation |= (u8)smfBit;
    } else {
        sForcedEffects &= (u8)~smfBit;
    }
}

bool PortPpuMzm_DebugGetForceEffect(unsigned smfBit) {
    return (sForcedEffects & smfBit) != 0;
}

void PortPpuMzm_DebugSetMisc(unsigned bit, bool on) {
    if (on) {
        gEquipment.suitMisc |= (u8)bit;
        gEquipment.suitMiscActivation |= (u8)bit;
    } else {
        gEquipment.suitMisc &= (u8)~bit;
        gEquipment.suitMiscActivation &= (u8)~bit;
        sForcedEffects &= (u8)~bit;
    }
}

void PortPpuMzm_DebugCheatTick(void) {
    if (gMainGameMode != GM_INGAME) return;

    if (sDebugGod) {
        gEquipment.currentEnergy = gEquipment.maxEnergy;
        gEquipment.currentMissiles = gEquipment.maxMissiles;
        gEquipment.currentSuperMissiles = gEquipment.maxSuperMissiles;
        gEquipment.currentPowerBombs = gEquipment.maxPowerBombs;
    }

    /* The continuous-effect forcing itself lives in PORT-guarded hooks in
     * src/samus.c (SamusCheckShinesparking / ...AffectingEnvironment), which
     * read PortPpuMzm_DebugGetForceEffect(). Here we only keep the owned/
     * activation bits set so those hooks and the HUD stay consistent. */
    if (sForcedEffects & SMF_SPEEDBOOSTER) {
        gEquipment.suitMisc |= SMF_SPEEDBOOSTER;
        gEquipment.suitMiscActivation |= SMF_SPEEDBOOSTER;
    }
    if (sForcedEffects & SMF_SCREW_ATTACK) {
        /* Also grant Space Jump so the aerial spinning-screw is sustained. */
        gEquipment.suitMisc |= (SMF_SCREW_ATTACK | SMF_SPACE_JUMP);
        gEquipment.suitMiscActivation |= (SMF_SCREW_ATTACK | SMF_SPACE_JUMP);
    }
}

/* ---- Equipment -------------------------------------------------------
 * Every flag exists twice in gEquipment: the "owned" set (beamBombs /
 * suitMisc) and the "currently switched on" set (beamBombsActivation /
 * suitMiscActivation) that the pause screen toggles. A debug toggle has to
 * move both together, or an item reads as collected but does nothing (or,
 * worse, as active but not owned).
 * -------------------------------------------------------------------- */
void PortPpuMzm_DebugGetEquipment(unsigned* outBeams, unsigned* outMisc) {
    if (outBeams) *outBeams = gEquipment.beamBombs;
    if (outMisc) *outMisc = gEquipment.suitMisc;
}

void PortPpuMzm_DebugToggleBeam(unsigned bit) {
    if (gEquipment.beamBombs & bit) {
        gEquipment.beamBombs &= (u8)~bit;
        gEquipment.beamBombsActivation &= (u8)~bit;
    } else {
        gEquipment.beamBombs |= (u8)bit;
        gEquipment.beamBombsActivation |= (u8)bit;
    }
}

void PortPpuMzm_DebugToggleMisc(unsigned bit) {
    if (gEquipment.suitMisc & bit) {
        gEquipment.suitMisc &= (u8)~bit;
        gEquipment.suitMiscActivation &= (u8)~bit;
        sForcedEffects &= (u8)~bit;
    } else {
        gEquipment.suitMisc |= (u8)bit;
        gEquipment.suitMiscActivation |= (u8)bit;
    }
}

void PortPpuMzm_DebugSetAllEquipment(bool on) {
    u8 v = on ? 0xFF : 0x00;
    gEquipment.beamBombs = v;
    gEquipment.beamBombsActivation = v;
    gEquipment.suitMisc = v;
    gEquipment.suitMiscActivation = v;
    /* suitType tracks which suit sprite Samus wears; keep it consistent with
     * having/not having the suit upgrades rather than leaving a fully
     * powered suit on a Samus who now owns nothing. */
    gEquipment.suitType = on ? SUIT_FULLY_POWERED : SUIT_NORMAL;
}

/* Ammo/energy. The maxima are the real 100%-run totals for normal
 * difficulty, not arbitrary big numbers, so a debug-maxed file behaves like
 * a legitimately completed one. */
#define PORT_DEBUG_MAX_ENERGY        1299
#define PORT_DEBUG_MAX_MISSILES      250
#define PORT_DEBUG_MAX_SUPERS        30
#define PORT_DEBUG_MAX_POWER_BOMBS   30

void PortPpuMzm_DebugSetAmmo(bool full) {
    if (full) {
        gEquipment.maxEnergy = PORT_DEBUG_MAX_ENERGY;
        gEquipment.maxMissiles = PORT_DEBUG_MAX_MISSILES;
        gEquipment.maxSuperMissiles = PORT_DEBUG_MAX_SUPERS;
        gEquipment.maxPowerBombs = PORT_DEBUG_MAX_POWER_BOMBS;
    } else {
        gEquipment.maxEnergy = 99;
        gEquipment.maxMissiles = 0;
        gEquipment.maxSuperMissiles = 0;
        gEquipment.maxPowerBombs = 0;
    }
    gEquipment.currentEnergy = gEquipment.maxEnergy;
    gEquipment.currentMissiles = gEquipment.maxMissiles;
    gEquipment.currentSuperMissiles = gEquipment.maxSuperMissiles;
    gEquipment.currentPowerBombs = gEquipment.maxPowerBombs;
}

/* Tops up the current counts without touching the capacities. */
void PortPpuMzm_DebugRefillAmmo(void) {
    gEquipment.currentEnergy = gEquipment.maxEnergy;
    gEquipment.currentMissiles = gEquipment.maxMissiles;
    gEquipment.currentSuperMissiles = gEquipment.maxSuperMissiles;
    gEquipment.currentPowerBombs = gEquipment.maxPowerBombs;
}

void PortPpuMzm_DebugGetAmmoText(char* out, int outSize) {
    if (!out || outSize <= 0) return;
    snprintf(out, (size_t)outSize, "E%u/%u M%u S%u P%u",
             gEquipment.currentEnergy, gEquipment.maxEnergy,
             gEquipment.maxMissiles, gEquipment.maxSuperMissiles,
             gEquipment.maxPowerBombs);
}


/* ---------------------------------------------------------------------
 * World pixel shown at screen (0,0).
 *
 * This used to back PortPpuMzm_ClipIsSolidAtScreen, a per-tile clipdata
 * probe the stereo renderer asked for depth. That approach is gone --
 * clipdata is a collision grid, not an outline of what is drawn, and using
 * it per tile tore ramps and rock ledges apart every 16px (see
 * EffectiveBgPriority in port_gpu_renderer.c for the full history). Depth
 * now comes from BG priority reconciled against the sprite priorities on
 * screen, and needs no clipdata at all.
 *
 * Two consumers remain. The scene recorder's clip grid, which is still
 * worth capturing: clipdata and the camera live in EWRAM, so without it no
 * recording can say where in the world a frame actually was, which is what
 * made the ramp diagnosis possible in the first place. And the GPU
 * renderer's per-block layer corrections (port_layer_fixes.c), which are
 * keyed to absolute room-block positions and so need the world position of
 * screen (0,0) to turn an on-screen tile back into a room block.
 *
 * The world position of screen pixel 0 comes from the BG SCROLL REGISTERS,
 * not from the camera. The two agree only while the camera is still:
 * decoding a ledge-grab recording (191 samples carrying both) showed
 * camPixelX - BG1HOFS == 0 always, but camPixelY - BG1VOFS drifting to as
 * much as -37 whenever the camera moved vertically -- over two 16px clip
 * blocks. The registers are 9 bits, so they only give position modulo 512;
 * the camera resolves WHICH 512px window we are in, exact as long as the
 * two never disagree by more than 256px, orders of magnitude beyond the
 * observed drift. BG1 is the layer clipdata belongs to, so its registers
 * are the ones that define the grid's alignment.
 * ------------------------------------------------------------------- */
void PortPpuMzm_ScreenOrigin(int* outX, int* outY) {
    const int kScrollMask = 0x1FF;
    int bg1Hofs = (int)(gIoMem[0x14] | (gIoMem[0x15] << 8)) & kScrollMask;
    int bg1Vofs = (int)(gIoMem[0x16] | (gIoMem[0x17] << 8)) & kScrollMask;
    int camPixelX = (int)gCamera.xPosition / SUB_PIXEL_RATIO;
    int camPixelY = (int)gCamera.yPosition / SUB_PIXEL_RATIO;
    *outX = camPixelX + ((((bg1Hofs - camPixelX) & kScrollMask) + 256) & kScrollMask) - 256;
    *outY = camPixelY + ((((bg1Vofs - camPixelY) & kScrollMask) + 256) & kScrollMask) - 256;
}

/* ---------------------------------------------------------------------
 * Clip/camera snapshot for the scene recorder.
 *
 * mzm-rec.bin captures VRAM, IO, palettes and OAM -- everything the PPU
 * draws from -- but the clipdata and the camera live in EWRAM, so nothing
 * in a recording could confirm whether PortPpuMzm_ClipIsSolidAtScreen picks
 * the same block the renderer drew. That made the "tiles flip depth plane
 * for a frame while the camera moves" report impossible to verify offline;
 * this block is what makes it checkable.
 *
 * Layout is fixed and appended after VRAM in each sample (recorder magic
 * 'MZM6' -- 'MZM3' added this block, 'MZM4' added area/room, 'MZM5' added the
 * cutscene id, 'MZM6' added the cutscene STAGE). The clip grid covers the
 * visible screen plus one block of slack on each axis, at the same block
 * resolution clipdata uses.
 * ------------------------------------------------------------------- */
#define PORT_CLIPREC_COLS 17
#define PORT_CLIPREC_ROWS 12

/* The sample stride is a fixed part plus this block. Every parser locates
 * sample N by scanning for the header magic, so N*stride must never drift
 * off the alignment the scanner steps on -- keep the block a multiple of 4.
 * (It was 230 under 'MZM5' and off-grid, which made 2-byte scanning
 * necessary; 'MZM6' brought it back to 232.) */
_Static_assert((28 + PORT_CLIPREC_COLS * PORT_CLIPREC_ROWS + 12 * 2) % 4 == 0,
               "clip record block must stay 4-byte aligned");

/* Which page of a montage cutscene is on screen. A single GM_CUTSCENE or
 * GM_TOURIAN_ESCAPE runs many pages back to back, and several of them use
 * the SAME layer config (e.g. the Tourian escape's flight pages are all
 * "BG0 + OBJ, priority 0"), so the DISPCNT/BGCNT layout signature cannot
 * tell them apart -- an override keyed to the layout alone hits every page.
 * The per-mode state machine's stage index does distinguish them. */
int PortPpuMzm_CutsceneStage(void) {
    if (sNonGameplayRamPointer == NULL) return 0;
    switch (gMainGameMode) {
        case GM_TOURIAN_ESCAPE: return (int)TOURIAN_ESCAPE_DATA.stage;
        case GM_CUTSCENE:       return (int)CUTSCENE_DATA.timeInfo.stage;
        default:                return 0;
    }
}

void PortPpuMzm_GetClipRecordBlock(uint8_t* out) {
    /* 28 bytes of scalars, then the grid. gCurrentCutscene is already declared
     * (include/structs/cutscene.h, reached via the game headers above). */
    uint16_t* w = (uint16_t*)out;
    w[0] = gCamera.xPosition;
    w[1] = gCamera.yPosition;
    w[2] = gSamusData.xPosition;
    w[3] = gSamusData.yPosition;
    w[4] = (uint16_t)gBgPointersAndDimensions.clipdataWidth;
    w[5] = (uint16_t)gBgPointersAndDimensions.clipdataHeight;
    w[6] = (uint16_t)gMainGameMode;
    w[7] = (uint16_t)gSamusData.pose;
    /* w[8]/w[9]: the origin the grid below was sampled from.
     * w[10]/w[11]: area and room. A tile correction has to be keyed to a
     * position in a ROOM, not on screen, or it moves with the camera --
     * without these a recording cannot say which room it is of, and the key
     * has to be filled in by hand afterwards.
     * w[12]: the active cutscene id (Cutscene enum), for GM_CUTSCENE. Lets an
     * offline tool key a per-cutscene depth override to the right scene;
     * meaningless outside GM_CUTSCENE but always written.
     * w[13]: the montage-page stage index (see PortPpuMzm_CutsceneStage),
     * so an override can target ONE page of a cutscene whose pages share a
     * layer config. 0 outside a staged cutscene. */

    int originX, originY;
    PortPpuMzm_ScreenOrigin(&originX, &originY);
    w[8] = (uint16_t)originX;
    w[9] = (uint16_t)originY;
    w[10] = (uint16_t)gCurrentArea;
    w[11] = (uint16_t)gCurrentRoom;
    w[12] = (uint16_t)gCurrentCutscene;
    w[13] = (uint16_t)PortPpuMzm_CutsceneStage();

    uint8_t* grid = out + 28;
    int baseBlockX = originX / PIXEL_PER_BLOCK;
    int baseBlockY = originY / PIXEL_PER_BLOCK;

    for (int r = 0; r < PORT_CLIPREC_ROWS; ++r) {
        for (int c = 0; c < PORT_CLIPREC_COLS; ++c) {
            int bx = baseBlockX + c;
            int by = baseBlockY + r;
            uint8_t v = 0xFF; /* out of bounds marker */
            if (bx >= 0 && by >= 0 &&
                (u32)bx < gBgPointersAndDimensions.clipdataWidth &&
                (u32)by < gBgPointersAndDimensions.clipdataHeight &&
                gTilemapAndClipPointers.pClipCollisions != NULL) {
                v = gTilemapAndClipPointers.pClipCollisions[GET_CLIP_BLOCK_((u32)bx, (u32)by)];
            }
            grid[r * PORT_CLIPREC_COLS + c] = v;
        }
    }

    /* After the grid (nothing before it moves, so older parsers still read
     * the rest): how the WIDE view was worked out for this frame. 12 words:
     * the renderer's view (slide x/y, room masks l/r/t/b, flags -- see
     * Port_GpuRenderer_GetWideRecord), gSubGameMode1, gColorFading type and
     * stage, the door tunnel's phase (PortPpuMzm_DoorTunnel) and its x/y. */
    extern void Port_GpuRenderer_GetWideRecord(int16_t out[7]);
    int16_t* wide = (int16_t*)(grid + PORT_CLIPREC_COLS * PORT_CLIPREC_ROWS);
    Port_GpuRenderer_GetWideRecord(wide);
    int tunX = 0, tunY = 0;
    wide[7] = (int16_t)gSubGameMode1;
    wide[8] = (int16_t)gColorFading.type;
    wide[9] = (int16_t)gColorFading.stage;
    wide[10] = (int16_t)PortPpuMzm_DoorTunnel(&tunX, &tunY, NULL, NULL, NULL);
    wide[11] = (int16_t)tunX;
    (void)tunY;
}

int PortPpuMzm_GetClipRecordBlockSize(void) {
    return 28 + PORT_CLIPREC_COLS * PORT_CLIPREC_ROWS + 12 * 2;
}

/* ---------------------------------------------------------------------
 * Visible item-tank blocks in the current room.
 *
 * The in-world Energy / Missile / Super Missile / Power Bomb tank icon is an
 * ANIMATED tile: its pixels are streamed at runtime from sAnimatedTankGfx
 * (data/animated_tiles_data.c), not stored in any BG block map. So the tank
 * is invisible to tools/layer-workbench (which renders BG0..2 statically from
 * the tileset) and cannot be moved with a port_layer_fixes.inc entry. Yet as
 * a BG1 tile it inherits the BG play-plane depth tier (nearer to the viewer
 * than the world-sprite plane), which reads as the tank floating in front of
 * Samus instead of sitting with her.
 *
 * This is NOT the abandoned per-tile clipdata depth probe (see
 * PortPpuMzm_ScreenOrigin's history): that inferred a layer from raw
 * solidity and tore ramps apart every 16px. Here we scan the room's clip
 * behaviours, keep only the exact blocks whose behaviour is a visible tank
 * (0x38..0x3F), and the renderer lifts only those blocks. Same "keyed to an
 * absolute room-block position, human-scoped" model as port_layer_fixes,
 * just derived from the data instead of hand-authored.
 *
 * Rescanned every frame, not once per room: a HIDDEN tank (0x34..0x37) looks
 * like wall and is excluded, but shooting it rewrites its clipdata to the
 * visible behaviour mid-room (BlockProcess -> BLOCK_LIFE_TYPE_TANK,
 * src/block.c). A once-per-room scan missed that and left the freshly
 * revealed tank at the wall's depth plane. A full block-grid walk is a few
 * thousand u16 reads -- noise next to the frame it feeds.
 * ------------------------------------------------------------------- */
#define PORT_TANK_MAX 24

static uint16_t sTankBlockX[PORT_TANK_MAX];
static uint16_t sTankBlockY[PORT_TANK_MAX];
static int sTankCount = 0;

void PortPpuMzm_ScanRoomTanks(void) {
    sTankCount = 0;

    const uint16_t* decomp = gBgPointersAndDimensions.pClipDecomp;
    const uint16_t* behaviours = gTilemapAndClipPointers.pClipBehaviors;
    const int w = (int)gBgPointersAndDimensions.clipdataWidth;
    const int h = (int)gBgPointersAndDimensions.clipdataHeight;
    if (decomp == NULL || behaviours == NULL || w <= 0 || h <= 0)
        return;

    for (int by = 0; by < h; ++by) {
        for (int bx = 0; bx < w; ++bx) {
            uint16_t b = behaviours[decomp[by * w + bx]];
            if (b >= CLIP_BEHAVIOR_ENERGY_TANK &&
                b <= CLIP_BEHAVIOR_UNDERWATER_POWER_BOMB_TANK) {
                if (sTankCount < PORT_TANK_MAX) {
                    sTankBlockX[sTankCount] = (uint16_t)bx;
                    sTankBlockY[sTankCount] = (uint16_t)by;
                    ++sTankCount;
                }
            }
        }
    }
}

int PortPpuMzm_RoomTankCount(void) {
    return sTankCount;
}

void PortPpuMzm_RoomTankBlock(int i, int* blockX, int* blockY) {
    *blockX = (int)sTankBlockX[i];
    *blockY = (int)sTankBlockY[i];
}

bool PortPpuMzm_IsVisibleTankBlock(int blockX, int blockY) {
    for (int i = 0; i < sTankCount; ++i) {
        if (sTankBlockX[i] == (uint16_t)blockX && sTankBlockY[i] == (uint16_t)blockY)
            return true;
    }
    return false;
}

/* ------------------------------------------------------------------
 * Door footprint depth unification
 *
 * A doorway reads as one object -- the recessed tunnel on BG1 plus the
 * lintel/sill ledge that MZM paints on BG2 right above and below it.
 * BG1 lands on the play plane, BG2 a plane back, so in stereo the ledge
 * detaches from the frame and Samus wedges into the gap.
 *
 * The renderer pulls the DEPTH of the opaque BG0/BG1/BG2 tiles in this
 * footprint onto the play plane (BG3, the parallax backdrop, is never
 * touched). The footprint per door is a tight rectangle, NO horizontal
 * growth:
 *
 *   - the door's own column span (xStart..xEnd) from sAreaDoorsPointers,
 *     widened one block each side -- the outboard one is wall (BG1,
 *     already on the play plane, a no-op), the inboard one is the trim
 *     next to the hatch,
 *   - ONLY the PORT_DOOR_DEPTH_MARGIN_Y rows above yStart and below yEnd,
 *     the lintel/sill ledge. The door's own rows (yStart..yEnd) are the
 *     animated hatch capsule and are left on their own plane -- pulling
 *     them forward made the backdrop revealed as the capsule opens draw on
 *     top of the scene.
 *
 * An earlier version grew each row along the BG2 run to avoid cutting a
 * ledge mid-run. It reached too far and dragged actual background forward,
 * so it is gone: only the blocks immediately around the opening move.
 *
 * Rebuilt on room change; the on-screen test and per-tile lookup run per
 * frame. Stored as per-row spans (all rows the same x range here) so the
 * lookup and the seam-free bbox share one path.
 * ------------------------------------------------------------------ */
#define PORT_DOOR_DEPTH_SPANS 96
#define PORT_DOOR_DEPTH_MARGIN_Y 2   /* rows added above and below */

static uint16_t sDoorSpanY[PORT_DOOR_DEPTH_SPANS];
static uint16_t sDoorSpanX0[PORT_DOOR_DEPTH_SPANS];
static uint16_t sDoorSpanX1[PORT_DOOR_DEPTH_SPANS];
static int sDoorSpanCount = 0;

void PortPpuMzm_SetDoorDepthRoom(int area, int room) {
    sDoorSpanCount = 0;
    if (area < 0 || area >= AREA_ENTRY_COUNT) return;
    const struct Door* doors = sAreaDoorsPointers[area];
    if (doors == NULL) return;

    const int bw = (int)gBgPointersAndDimensions.backgrounds[1].width;

    /* Clipdata, for telling a real hatch door from a plain screen edge.
     * The Door table also holds vertical "fall through the floor / climb
     * through the ceiling" transitions and area seams; their rect is a wide
     * strip along a room edge, and pulling all of that forward looked wrong
     * (a free-fall transition floated the whole floor band). A genuine door
     * has CLIP_BEHAVIOR_DOOR_TRANSITION somewhere in its rect; the edge
     * transitions have CLIP_BEHAVIOR_VERTICAL_* or nothing. */
    const uint16_t* clip = gBgPointersAndDimensions.pClipDecomp;
    const uint16_t* clipBeh = gTilemapAndClipPointers.pClipBehaviors;
    const int cw = (int)gBgPointersAndDimensions.clipdataWidth;
    const int ch = (int)gBgPointersAndDimensions.clipdataHeight;

    for (int i = 0; i < PORT_WARP_MAX_DOORS; ++i) {
        const struct Door* d = &doors[i];
        if (d->type == DOOR_TYPE_NONE) break;          /* table terminator */
        if (d->sourceRoom != (u8)room) continue;

        /* Require a DOOR_TRANSITION block inside the door's own rect. */
        if (clip != NULL && clipBeh != NULL && cw > 0 && ch > 0) {
            bool isHatchDoor = false;
            for (int cy = (int)d->yStart; cy <= (int)d->yEnd && !isHatchDoor; ++cy) {
                if (cy < 0 || cy >= ch) continue;
                for (int cx = (int)d->xStart; cx <= (int)d->xEnd; ++cx) {
                    if (cx < 0 || cx >= cw) continue;
                    if (clipBeh[clip[cy * cw + cx]] == CLIP_BEHAVIOR_DOOR_TRANSITION) {
                        isHatchDoor = true;
                        break;
                    }
                }
            }
            if (!isHatchDoor) continue;
        }

        int dx0 = (int)d->xStart - 1, dx1 = (int)d->xEnd + 1;
        /* A door against the room's edge: the trim runs on to the edge, and
         * the block or two past the outboard one would otherwise stay on
         * their own plane -- a lone tile at the end of the lintel/sill that
         * sits at a different depth from the rest. Take them too. */
        if (dx0 <= 2) dx0 = 0;
        if (bw > 0 && dx1 >= bw - 3) dx1 = bw - 1;
        int y0 = (int)d->yStart - PORT_DOOR_DEPTH_MARGIN_Y;
        int y1 = (int)d->yEnd   + PORT_DOOR_DEPTH_MARGIN_Y;
        if (y0 < 0) y0 = 0;

        for (int y = y0; y <= y1; ++y) {
            /* Only the lintel/sill trim -- the MARGIN_Y rows above yStart and
             * below yEnd. The door's own rows (yStart..yEnd) are the animated
             * hatch capsule; pulling those forward makes the background
             * revealed as the capsule opens draw ON TOP of the scene even
             * though it is drawn behind. The capsule's presence is still what
             * puts the trim rows in the footprint -- but the capsule tiles
             * themselves stay on their own plane. */
            if (y >= (int)d->yStart && y <= (int)d->yEnd) continue;
            if (sDoorSpanCount >= PORT_DOOR_DEPTH_SPANS) return;
            sDoorSpanY[sDoorSpanCount]  = (uint16_t)y;
            sDoorSpanX0[sDoorSpanCount] = (uint16_t)dx0;
            sDoorSpanX1[sDoorSpanCount] = (uint16_t)dx1;
            ++sDoorSpanCount;
        }
    }
}

int PortPpuMzm_DoorDepthCount(void) { return sDoorSpanCount; }

/* Span i of the door footprint: block row y, block columns x0..x1. */
void PortPpuMzm_DoorDepthSpan(int i, int* y, int* x0, int* x1) {
    *y = (int)sDoorSpanY[i];
    *x0 = (int)sDoorSpanX0[i];
    *x1 = (int)sDoorSpanX1[i];
}

/* Any door span overlapping the block-space box [bx0,bx1] x [by0,by1]. */
bool PortPpuMzm_DoorDepthInView(int bx0, int by0, int bx1, int by1) {
    for (int i = 0; i < sDoorSpanCount; ++i) {
        int y = (int)sDoorSpanY[i];
        if (y >= by0 && y <= by1 &&
            (int)sDoorSpanX0[i] <= bx1 && (int)sDoorSpanX1[i] >= bx0)
            return true;
    }
    return false;
}

bool PortPpuMzm_IsDoorDepthBlock(int blockX, int blockY) {
    for (int i = 0; i < sDoorSpanCount; ++i) {
        if ((int)sDoorSpanY[i] == blockY &&
            blockX >= (int)sDoorSpanX0[i] && blockX <= (int)sDoorSpanX1[i])
            return true;
    }
    return false;
}

/* ---------------------------------------------------------------------
 * The door tunnel of a door transition (COLOR_FADING_DOOR_TRANSITION and its
 * white variant). BG3 holds the tunnel graphic from
 * ColorFadingProcess_DoorTransition stage 3 -- still over the room being
 * left -- until ColorFadingUpdate_DoorTransition has slid it to the new
 * room's door and put the room's BG3 back (its stage 3 ends by jumping to
 * stage 5). The slide is gBackgroundPositions.doorTransition moving toward
 * gDoorPositionStart: vertically first (stage 1), then horizontally
 * (stage 3). Positions are BG3 scroll, in pixels.
 * ------------------------------------------------------------------- */
int PortPpuMzm_DoorTunnel(int* outX, int* outY, int* outTargetX, int* outTargetY, int* outPause) {
    if (gMainGameMode != GM_INGAME) return PORT_DOOR_TUNNEL_NONE;
    if (gColorFading.type != COLOR_FADING_DOOR_TRANSITION && gColorFading.type != COLOR_FADING_WHITE)
        return PORT_DOOR_TUNNEL_NONE;
    int phase = PORT_DOOR_TUNNEL_NONE;
    if (gSubGameMode1 == SUB_GAME_MODE_LOADING_ROOM && gColorFading.stage >= 3)
        phase = PORT_DOOR_TUNNEL_OLD_ROOM;
    else if (gSubGameMode1 == SUB_GAME_MODE_DOOR_TRANSITION && gColorFading.stage <= 3)
        phase = PORT_DOOR_TUNNEL_SLIDING;
    if (outX) *outX = gBackgroundPositions.doorTransition.x;
    if (outY) *outY = gBackgroundPositions.doorTransition.y;
    if (outTargetX) *outTargetX = gDoorPositionStart.x;
    if (outTargetY) *outTargetY = gDoorPositionStart.y;
    if (outPause) {
        /* ColorFadingUpdate_DoorTransition: stage 2 waits while unk_3 counts
         * 1, 2, 3, then stage 3 slides horizontally. */
        *outPause = gColorFading.stage < 2 ? 0
                  : gColorFading.stage == 2 ? (gColorFading.unk_3 > PORT_DOOR_TUNNEL_PAUSE_FRAMES
                                                   ? PORT_DOOR_TUNNEL_PAUSE_FRAMES : gColorFading.unk_3)
                  : PORT_DOOR_TUNNEL_PAUSE_FRAMES;
    }
    return phase;
}

/* ---------------------------------------------------------------------
 * WIDE view: how each BG scrolls against the camera.
 *
 * The WIDE view slides past the GBA camera near a scroll limit (see
 * ComputeWideView in port_gpu_renderer.c). Sliding every layer by the same
 * amount is only right for the layers that follow the camera 1:1; a parallax
 * backdrop that moves at half the camera's speed has to slide half as far, or
 * it keeps drifting while the scenery in front is held still. These are the
 * game's own rules (ScrollBg3, ScrollBg2, ScrollUpdateEffectAndHazePosition
 * in src/scroll.c), reduced to "layer moves 1/divisor as far as the camera".
 * 0 means the layer does not follow the camera at all.
 * ------------------------------------------------------------------- */
int PortPpuMzm_WideLayerDivisor(int bg, int vertical) {
    /* The door tunnel is drawn on BG3 in screen space, over the door: it
     * slides with the view like the room itself, whatever the room's own
     * BG3 does. */
    if (bg == 3 && PortPpuMzm_DoorTunnel(NULL, NULL, NULL, NULL, NULL) != PORT_DOOR_TUNNEL_NONE) return 1;
    switch (bg) {
        case 0:
            if (gCurrentRoomEntry.bg0Prop & BG_PROP_RLE_COMPRESSED)
                return (gCurrentRoomEntry.bg0Prop == 0x11 && !vertical) ? 2 : 1;
            if (gCurrentRoomEntry.effectY == USHORT_MAX && gCurrentRoomEntry.bg0Prop == BG_PROP_CLOSE_UP)
                return 0;
            return 1;
        case 1:
            return 1;
        case 2:
            return (gCurrentRoomEntry.bg2Prop & BG_PROP_RLE_COMPRESSED) ? 1 : 0;
        case 3: {
            const u32 types = ScrollGetBg3Scroll();
            const u32 type = vertical ? HIGH_SHORT(types) : LOW_BYTE(types);
            if (type == BG3_SCROLLING_TYPE_NORMAL) return 1;
            if (type == BG3_SCROLLING_TYPE_HALVED)
                /* Counted from the room's bottom, "halved" is really a quarter. */
                return (vertical && gCurrentRoomEntry.bg3FromBottomFlag) ? 4 : 2;
            if (type == BG3_SCROLLING_TYPE_QUARTERED) return 4;
            return 0;
        }
        default:
            return 1;
    }
}

#ifdef PORT_DEBUG_TOOLS
/* One line of game state for the remote control (SITE STATE, debug_link_3ds.c), which cannot
 * include the game's headers next to <3ds.h>. Read from the logic thread, between frames. */
void Port_DebugLink_StateLine(char* out, unsigned cap) {
    snprintf(out, cap,
             "mode=%02X sub=%02X area=%u room=%u x=%u y=%u pose=%02X energy=%u/%u missiles=%u/%u supers=%u/%u pbs=%u/%u",
             (unsigned)gMainGameMode, (unsigned)gSubGameMode1, (unsigned)gCurrentArea, (unsigned)gCurrentRoom,
             (unsigned)gSamusData.xPosition, (unsigned)gSamusData.yPosition, (unsigned)gSamusData.pose,
             (unsigned)gEquipment.currentEnergy, (unsigned)gEquipment.maxEnergy, (unsigned)gEquipment.currentMissiles,
             (unsigned)gEquipment.maxMissiles, (unsigned)gEquipment.currentSuperMissiles,
             (unsigned)gEquipment.maxSuperMissiles, (unsigned)gEquipment.currentPowerBombs,
             (unsigned)gEquipment.maxPowerBombs);
}
#endif
