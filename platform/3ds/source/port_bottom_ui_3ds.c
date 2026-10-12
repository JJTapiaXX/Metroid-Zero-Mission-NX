#include "port_bottom_ui_3ds.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "platform_gpu_3ds.h"
#include "port_debug_tools.h"
#include "port_debug_log.h"
#include "port_save_state.h"
#include "port_state_thumb.h"
#include "port_updater_3ds.h"

/* GBA & MZM minimap and state globals */
extern uint16_t gDecompressedMinimapVisitedTiles[32 * 32];
extern uint16_t gDecompressedMinimapData[32 * 32];
extern uint32_t gVisitedMinimapTiles[8 * 32];
extern uint32_t gMinimapTilesWithObtainedItems[8 * 32];
extern uint8_t gMinimapX;
extern uint8_t gMinimapY;
extern uint8_t gCurrentArea;
extern uint8_t gCurrentRoom;
extern int8_t gLanguage;

/* Equipment struct (from include/structs/samus.h) — declared here to avoid
 * pulling in the GBA types.h which conflicts with the 3DS SDK headers. */
struct Equipment {
    uint16_t maxEnergy;
    uint16_t maxMissiles;
    uint8_t maxSuperMissiles;
    uint8_t maxPowerBombs;
    uint16_t currentEnergy;
    uint16_t currentMissiles;
    uint8_t currentSuperMissiles;
    uint8_t currentPowerBombs;
    uint8_t beamBombs;
    uint8_t beamBombsActivation;
    uint8_t suitMisc;
    uint8_t suitMiscActivation;
    uint8_t downloadedMapStatus;
    uint8_t lowHealth;
    uint8_t suitType;
    uint8_t grabbedByMetroid;
};
extern struct Equipment gEquipment;

/* Weapon state — missilesSelected is at offset 3 in the real struct.
 * We only need this one field, so declare a matching stub. */
struct WeaponInfoStub {
    uint8_t _pad[3];            /* diagonalAim + newProjectile + weaponHighlighted */
    uint8_t missilesSelected;   /* FALSE = normals, TRUE = supers */
};
extern struct WeaponInfoStub gSamusWeaponInfo;

/* ROM-resolved minimap graphic and palette tables */
extern const uint8_t(*p_sMinimapTilesGfx)[5120];
extern const uint16_t(*p_sMinimapTilesPal)[5 * 16];

/* External area map loaders */
extern void PauseScreenGetMinimapData(uint8_t area, uint16_t* dst);
extern void MinimapSetDownloadedTiles(uint8_t area, uint16_t* dst);

/* Chozo target tracking */
struct ChozoStatueTargetView {
    uint8_t statueArea;
    uint8_t statueXStart;
    uint8_t statueXEnd;
    uint8_t statueYStart;
    uint8_t statueYEnd;
    uint8_t startIcon;

    uint8_t targetArea;
    uint8_t targetX;
    uint8_t targetY;
    uint8_t endIcon;
    uint8_t _pad[2];
};
extern const struct ChozoStatueTargetView(*p_sChozoStatueTargets)[16];
extern int32_t ChozoStatueHintCheckTargetIsActivated(uint8_t target) __attribute__((weak));

/* sElevatorRoomPairs (struct ElevatorPair, include/structs/clipdata.h) -- the
 * map tile of every inter-area elevator on each of its two sides. Layout is
 * eight u8s; declared locally for the same reason as ChozoStatueTargetView,
 * to keep the game headers out of this TU. Resolved by
 * PortGen_clipdata_data_Init at startup. */
struct PortElevatorPair {
    uint8_t area1, room1, mapX1, mapY1;
    uint8_t area2, room2, mapX2, mapY2;
};
#define PORT_ELEVATOR_ROUTE_COUNT 9 /* ELEVATOR_ROUTE_COUNT, index 0 = NONE */
extern const struct PortElevatorPair(*p_sElevatorRoomPairs)[PORT_ELEVATOR_ROUTE_COUNT];

/* Area indices (constants/connection.h). CHOZODIA has no elevator route. */
enum {
    PORT_AREA_BRINSTAR = 0, PORT_AREA_KRAID, PORT_AREA_NORFAIR, PORT_AREA_RIDLEY,
    PORT_AREA_TOURIAN, PORT_AREA_CRATERIA, PORT_AREA_CHOZODIA, PORT_AREA_COUNT
};

/* Next elevator to take from `fromArea` toward `toArea`, as a tile on the
 * `fromArea`-side map. Breadth-first over the elevator graph (7 nodes, 8
 * edges) -- the game's own sChozoStatueTargetPath tables encode the same
 * routes but as a stateful multi-hop walker; the graph is small enough to
 * just search. `*outDir` is +1 when the elevator descends from here
 * (arrow points down), -1 when it climbs (arrow up). Returns false when no
 * route connects the two areas. */
static bool PortMap_RouteArrow(uint8_t fromArea, uint8_t toArea,
                               uint8_t* outX, uint8_t* outY, int* outDir) {
    if (!p_sElevatorRoomPairs) return false;
    if (fromArea >= PORT_AREA_COUNT || toArea >= PORT_AREA_COUNT) return false;
    if (fromArea == toArea) return false;

    int prevArea[PORT_AREA_COUNT];
    int prevRoute[PORT_AREA_COUNT];
    bool seen[PORT_AREA_COUNT] = { false };
    int queue[PORT_AREA_COUNT];
    int head = 0, tail = 0;

    seen[fromArea] = true;
    queue[tail++] = fromArea;
    while (head < tail) {
        int a = queue[head++];
        if (a == (int)toArea) break;
        for (int r = 1; r < PORT_ELEVATOR_ROUTE_COUNT; ++r) {
            const struct PortElevatorPair* p = &(*p_sElevatorRoomPairs)[r];
            int nb = -1;
            if (p->area1 == a) nb = p->area2;
            else if (p->area2 == a) nb = p->area1;
            if (nb < 0 || nb >= PORT_AREA_COUNT || seen[nb]) continue;
            seen[nb] = true;
            prevArea[nb] = a;
            prevRoute[nb] = r;
            queue[tail++] = nb;
        }
    }
    if (!seen[toArea]) return false;

    /* Walk the chain back until the step that leaves fromArea. */
    int cur = toArea;
    while (prevArea[cur] != (int)fromArea) cur = prevArea[cur];
    const struct PortElevatorPair* p = &(*p_sElevatorRoomPairs)[prevRoute[cur]];
    if (p->area1 == fromArea) {
        *outX = p->mapX1; *outY = p->mapY1; *outDir = +1; /* descend */
    } else {
        *outX = p->mapX2; *outY = p->mapY2; *outDir = -1; /* climb */
    }
    return true;
}

/* Platform and Config functions */
extern bool Port_Config_GetShowFps(void);
extern void Port_Config_SetShowFps(bool on);
extern int Port_Config_GetFpsPosition(void);
extern void Port_Config_CycleFpsPosition(void);
extern int Port_Config_Get3DSAspectRatio(void);
extern const char* Port_Config_Get3DSAspectRatioName(void);
extern void Port_Config_Cycle3DSAspectRatio(void);
extern int Port_Config_Get3DSDisplayStyle(void);
extern const char* Port_Config_Get3DSDisplayStyleName(void);
extern void Port_Config_Cycle3DSDisplayStyle(void);
extern bool Port_Config_GetHudOutside(void);
extern void Port_Config_ToggleHudOutside(void);
extern bool Port_Config_GetGbaBezel(void);
extern void Port_Config_ToggleGbaBezel(void);

extern bool Platform3DS_IsNew3DS(void);
extern double Port_PPU_3DS_CurrentFps(void);
extern bool Port_PPU_3DS_LastFrameUsedGpu(void);
extern void Port_GpuRenderer_GetLastFrameStats(int* outItems, int* outObjItems, int* outCacheSlots);

static PortBottomTab sCurrentTab = BOTTOM_TAB_MAP;
static uint32_t sStateModalId = 0;   /* open save-state detail modal (0 = none); see the ESTADO tab */
static uint32_t sFrameCounter = 0;

/* Bottom-screen redraw throttle.
 *
 * Every view here is rasterised from scratch each call: the 5x7 bitmap font
 * and DrawAuthenticMapTile both emit one C2D_DrawRectSolid per pixel run,
 * so a text-heavy DEBUG view or a zoomed-out MAP view is several thousand
 * immediate-mode quads assembled on the main thread every frame. With the
 * game frame already sitting on the 16.6ms vsync edge that was enough to
 * push it over -- MAP at 1X measured ~30fps, at 3X (far fewer cells on
 * screen) a full 60. None of this content needs 60Hz: redraw it at ~20Hz
 * and reuse the persistent render target on the frames in between. A
 * genuine change (tab switch, zoom, pan, any touch) forces the next frame
 * to redraw immediately via Port_BottomUI_MarkDirty so interaction still
 * feels instant. */
/* frames between idle bottom-screen redraws. 6 -> ~10Hz at 60fps: the map
 * follows Samus per map-cell (a cell change is seconds apart, not smooth
 * panning) so it does not need more, and the only ~4-6Hz animated bits
 * (Chozo-hint target pulse, post-boss statue flames, low-health blink) are
 * situational and still read fine at 10Hz. Halves the map's redraw cost on
 * the frames in between. Interaction still forces an immediate redraw via
 * Port_BottomUI_MarkDirty. */
#define PORT_BOTTOM_UI_REDRAW_INTERVAL 6
static bool sBottomUiDirty = true;
static uint32_t sBottomUiRedrawThrottle = 0;

/* System status shown top-right of the tab bar (clock, battery, wifi).
 * Battery/charge come from ptm:u (an IPC call, polled slowly); wifi
 * strength is a shared-memory read done at draw time. */
static bool sPtmuReady = false;
static uint8_t sBattLevel = 5;   /* 0..5 from PTMU_GetBatteryLevel */
static bool sBattCharging = false;
static uint32_t sStatusPollCtr = 0;

void Port_BottomUI_MarkDirty(void) { sBottomUiDirty = true; }

/* --- Notice toast ----------------------------------------------------------
 * A short, non-blocking message floating over the bottom screen, e.g. for a
 * game option the port cannot offer. Raised from the game-logic thread and
 * drawn by the present thread: the text is copied first and the start time
 * published last, so a half-written message is never drawn. It slides in,
 * holds, then fades out; Port_BottomUI_FrameTick forces a redraw on every
 * frame of those two animations and once more when it ends. */
#define NOTICE_SHOW_MS 3500u
#define NOTICE_ENTER_MS 180u
#define NOTICE_FADE_MS 900u

static char sNoticeTitle[40];
static char sNoticeBody[64];
static volatile uint64_t sNoticeStartMs; /* 0 = no notice */

/* A notice that stays for as long as it lasts, shown in the same box: a build arriving over the
 * debug FTP server and its install (ftp_server_3ds.h). A literal, so a change is a pointer change. */
extern const char* FtpServer_Notice(void);
extern bool FtpServer_InstallOnly(void);
extern void FtpServer_Address(char* out, unsigned cap);
extern void FtpServer_Cancel(void);
static const char* StandingNotice(void) {
    return FtpServer_Notice();
}

/* The install-only FTP server of a production build (dev-ftp.txt): its address and a CANCEL that
 * stops it until the game is opened again. Over every tab and window; only that button is live. */
#define DEVFTP_X 236
#define DEVFTP_Y 204
#define DEVFTP_W 76
#define DEVFTP_H 28

void Port_BottomUI_ShowNotice(const char* title, const char* body) {
    snprintf(sNoticeTitle, sizeof(sNoticeTitle), "%s", title ? title : "");
    snprintf(sNoticeBody, sizeof(sNoticeBody), "%s", body ? body : "");
    __atomic_store_n(&sNoticeStartMs, osGetTime(), __ATOMIC_RELEASE);
    sBottomUiDirty = true;
}

/* Milliseconds since the notice appeared, or -1 when none is showing. */
static int NoticeElapsedMs(void) {
    const uint64_t start = __atomic_load_n(&sNoticeStartMs, __ATOMIC_ACQUIRE);
    if (start == 0) return -1;
    const uint64_t elapsed = osGetTime() - start;
    return elapsed < NOTICE_SHOW_MS ? (int)elapsed : -1;
}

static void NoticeTick(void) {
    {
        static const char* sStandingSeen;
        static bool sDevFtpSeen;
        const char* standing = StandingNotice();
        const bool devFtp = FtpServer_InstallOnly();
        if (standing != sStandingSeen || devFtp != sDevFtpSeen) {
            sStandingSeen = standing;
            sDevFtpSeen = devFtp;
            sBottomUiDirty = true;
        }
    }
    const uint64_t start = __atomic_load_n(&sNoticeStartMs, __ATOMIC_ACQUIRE);
    if (start == 0) return;
    const int elapsed = NoticeElapsedMs();
    if (elapsed < 0) {
        /* Clear it only if no newer notice replaced it meanwhile. */
        uint64_t expected = start;
        __atomic_compare_exchange_n(&sNoticeStartMs, &expected, 0, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        sBottomUiDirty = true; /* one last frame without it */
    } else if (elapsed < (int)NOTICE_ENTER_MS || elapsed >= (int)(NOTICE_SHOW_MS - NOTICE_FADE_MS)) {
        sBottomUiDirty = true;
    }
}

/* Icon-tab layout, shared by the renderer and the touch handler (defined
 * lower, used by both). */
typedef struct { float x, w; PortBottomTab tab; int icon; } BottomTabSlot;
#define BOTTOM_TAB_SLOT_MAX 6
static int BottomTabLayout(BottomTabSlot slots[BOTTOM_TAB_SLOT_MAX]);

/* Update prompt geometry, shared by the renderer and the touch handler. The
 * OK button of the error prompt reuses the YES slot. */
#define UPD_BTN_Y0 138
#define UPD_BTN_Y1 166
#define UPD_YES_X0 44
#define UPD_YES_X1 148
#define UPD_NO_X0 172
#define UPD_NO_X1 276
/* "What's new" button of the install prompt, below YES/NO. */
#define UPD_NOTES_Y0 174
#define UPD_NOTES_Y1 198

/* Called once per frame (even on frames the UI is not redrawn) so time-based
 * state keeps advancing: the blink counter and the RA session pump. */
void Port_BottomUI_FrameTick(void) {
    extern void Port_RA_Update(void); /* port_retroachievements_3ds.h, included below */
    ++sFrameCounter;
    Port_RA_Update();
    NoticeTick();

    /* The prompt is raised by the updater's worker thread: redraw when it (or
     * the install progress it shows) changes instead of waiting for a tap. */
    {
        static int sLastPrompt = 0, sLastProgress = -1;
        int pr = (int)Port_Updater_GetPrompt();
        int pg = Port_Updater_GetProgress();
        if (pr != sLastPrompt || (pr == (int)UPDATER_PROMPT_PROGRESS && pg != sLastProgress)) {
            sLastPrompt = pr;
            sLastProgress = pg;
            sBottomUiDirty = true;
        }
    }

    /* Refresh battery ~every 2s; it moves far slower than the UI redraws.
     * ptm:u may not be up yet the first time Port_BottomUI_Init ran (service
     * bring-up order), so keep retrying the init here until it takes. */
    if ((sStatusPollCtr++ % 120u) == 0u) {
        if (!sPtmuReady && R_SUCCEEDED(ptmuInit())) sPtmuReady = true;
        if (sPtmuReady) {
            u8 lvl = 5, chg = 0;
            if (R_SUCCEEDED(PTMU_GetBatteryLevel(&lvl))) sBattLevel = lvl > 5 ? 5 : lvl;
            if (R_SUCCEEDED(PTMU_GetBatteryChargeState(&chg))) sBattCharging = (chg != 0);
        }
    }
    /* Save state jobs: redraw when one starts or ends (the "wait" message),
     * hand a freshly read screenshot to the modal that asked for it, and read
     * the state list the first time the tab is looked at. */
    {
        static PortSaveStateJob sLastJob = PORT_SS_JOB_NONE;
        const PortSaveStateJob job = Port_SaveState_CurrentJob();
        if (job != sLastJob) {
            sLastJob = job;
            sBottomUiDirty = true;
        }
        uint32_t thumbId = 0;
        const uint16_t* thumb = Port_SaveState_TakeThumb(&thumbId);
        if (thumb && thumbId == sStateModalId) {
            PortStateThumb_Set(thumb);
            sBottomUiDirty = true;
        }
        if (sCurrentTab == BOTTOM_TAB_STATE && !Port_SaveState_Scanned() && job == PORT_SS_JOB_NONE)
            Port_SaveState_RequestScan();
    }
#ifdef PORT_DEBUG_TOOLS_ACTIVE
    { extern void PortPpuMzm_DebugCheatTick(void); PortPpuMzm_DebugCheatTick(); }
#endif
}

#ifdef PORT_DEBUG_TOOLS_ACTIVE
static bool AnyDebugModalOpen(void); /* defined with the debug modal statics below */
#endif

/* Whether Port_BottomUI_Render should run this frame. Has the side effect of
 * advancing the throttle, so call exactly once per frame. */
bool Port_BottomUI_WantsRedraw(void) {
    if (sBottomUiDirty) {
        sBottomUiDirty = false;
        sBottomUiRedrawThrottle = 0;
        return true;
    }
    /* The map/FPS/battery want a ~20Hz refresh, but the DEBUG TOOLS modal
     * (14 bordered cells + text + the live LINEAR readout) is expensive
     * enough that redrawing it that often inside a stereo + haze frame
     * makes every third frame overrun the vblank -- a periodic present
     * hitch that reads as flicker on moving sprites. Its content changes
     * slowly, so throttle it hard when it is the thing on screen. */
    int interval = PORT_BOTTOM_UI_REDRAW_INTERVAL;
#ifdef PORT_DEBUG_TOOLS_ACTIVE
    if (AnyDebugModalOpen()) interval = 20; /* ~3Hz */
#endif
    if (++sBottomUiRedrawThrottle >= interval) {
        sBottomUiRedrawThrottle = 0;
        return true;
    }
    return false;
}

/* Modals & Overlays */
static bool sShowRemapModal = false;
static int sRemapSelectButtonIdx = -1; /* >= 0 when action picker popup is open */
static bool sShowCollectiblesModal = false;
/* When the collectibles modal is opened from the MAP tab's item badge, the
 * row for that area is highlighted; -1 means "opened from STATUS", no row
 * singled out. */
static int sCollectiblesHighlightArea = -1;
static bool sShowAchievementsModal = false;
/* Pack chooser shown ahead of the achievement list. Only ever opened when the
 * game actually has more than one set -- with a single set a chooser with one
 * entry in it is pure friction, so the LOGROS button goes straight to the
 * list (see OpenAchievementsUi). sAchFromPacks records which way we came in so
 * BACK returns to the chooser instead of closing the whole modal. */
static bool sShowAchPacksModal = false;
static bool sAchFromPacks = false;
static bool sShowRASettingsModal = false;
static bool sShowDisplayModal = false;
static bool sShowUpdateModal = false;

/* "What's new" viewer, opened from the UPDATES modal while an update is
 * waiting. The text comes from Port_Updater_CopyNotes (every release newer
 * than this build); it is word-wrapped once, on open, into sNotesLines. */
#define NOTES_WRAP_COLS   44
#define NOTES_MAX_LINES   200
#define NOTES_LINE_H      10.0f
#define NOTES_VIEW_Y0     46.0f
#define NOTES_VIEW_Y1     200.0f
static bool sShowNotesModal = false;
static char sNotesBuf[6144 + 2048];
static const char* sNotesLines[NOTES_MAX_LINES];
static int sNotesLineCount = 0;
static float sNotesScrollY = 0.0f;

static float NotesMaxScroll(void) {
    float content = (float)sNotesLineCount * NOTES_LINE_H;
    float view = NOTES_VIEW_Y1 - NOTES_VIEW_Y0;
    return content > view ? content - view : 0.0f;
}

static void OpenNotesModal(void) {
    char raw[6144];
    char* w = sNotesBuf;
    char* end = sNotesBuf + sizeof(sNotesBuf) - 1;
    const char* line = raw;

    Port_Updater_CopyNotes(raw, sizeof(raw));
    sNotesLineCount = 0;
    while (*line && sNotesLineCount < NOTES_MAX_LINES) {
        const char* nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        const char* seg = line;
        bool first = true;

        if (len == 0) { /* blank line between releases */
            if (w < end && sNotesLineCount < NOTES_MAX_LINES) {
                sNotesLines[sNotesLineCount++] = w;
                *w++ = '\0';
            }
        }
        while (len > 0 && sNotesLineCount < NOTES_MAX_LINES && w + NOTES_WRAP_COLS + 4 < end) {
            size_t room = first ? NOTES_WRAP_COLS : NOTES_WRAP_COLS - 2;
            size_t take = len;
            size_t k;

            if (take > room) {
                take = room;
                for (k = take; k > 0 && seg[k] != ' '; --k) {}
                if (k > 0) take = k;
            }
            sNotesLines[sNotesLineCount++] = w;
            if (!first) { *w++ = ' '; *w++ = ' '; }
            memcpy(w, seg, take);
            w += take;
            *w++ = '\0';
            seg += take;
            len -= take;
            while (len > 0 && *seg == ' ') { seg++; len--; }
            first = false;
        }
        if (!nl) break;
        line = nl + 1;
    }
    sNotesScrollY = 0.0f;
    sShowNotesModal = true;
}

#ifdef PORT_DEBUG_TOOLS_ACTIVE
/* DEBUG tab -> [HERRAMIENTAS] modal. Touchable equivalent of the L+R+<btn>
 * combos documented in docs/3ds-debug-tools.md: same entry points, just
 * reachable without memorizing (or mis-pressing) a hold-two-shoulders
 * chord mid-gameplay. The combos stay wired up in
 * Platform3DS_PollKeysIntoGba -- this is an addition, not a replacement,
 * since some of them (the scene recorder especially) are worth triggering
 * without taking a hand off the controller. */
static bool sShowDebugToolsModal = false;
static bool sShowDebugWarpModal = false;
static bool sShowDebugEquipModal = false;
static bool sShowSoundTestModal = false;
static void RenderSoundTestModal(int lang);
static void HandleSoundTestModalTouch(int x, int y);
static bool AnyDebugModalOpen(void) {
    return sShowDebugToolsModal || sShowDebugWarpModal || sShowDebugEquipModal || sShowSoundTestModal;
}
/* MAP tab: when armed from the tools menu, the next tap on the map canvas
 * warps to the door nearest that tile instead of panning. One-shot -- it
 * disarms itself on use -- so a stray tap can't teleport the player later. */
static bool sDebugMapWarpArmed = false;
/* Largest distance (squared, px) the stylus wandered from where it first
 * touched down, for the current touch. See the tap test in
 * Port_BottomUI_TouchReleased. */
static int sDebugMapWarpDevSq = 0;
/* Transient one-line feedback under the list ("PUNTO GUARDADO", ...).
 * Frame-counted rather than timed: this UI already has sFrameCounter and
 * no clock source of its own. */
static char sDebugToolsMsg[48] = { 0 };
static uint32_t sDebugToolsMsgUntil = 0;

extern void PortPpuMzm_DebugKillSamus(void);
extern void Port_GpuRenderer_DumpAtlas(const char* ppmPath, const char* csvPath);
extern bool Port_GpuRenderer_IsActive(void);
extern void Port_GpuRenderer_SetActive(bool active);
extern void Port_GpuRenderer_SetDepthTint(bool on);
extern bool Port_GpuRenderer_DepthTintEnabled(void);
extern void Port_DebugLog(const char* msg);
extern bool Port_DebugLog_IsEnabled(void);
extern void Port_DebugLog_SetBuffered(bool buffered);
extern bool Port_DebugLog_IsBuffered(void);
extern void Port_DebugLog_CycleMode(void);       /* OFF -> ALL -> GPU -> AUDIO -> PERF -> OFF */
extern const char* Port_DebugLog_ModeName(void); /* "OFF"/"ALL"/"GPU"/"AUDIO"/"PERF" */
extern void PortPpuMzm_DebugSaveWarpPoint(void);
extern bool PortPpuMzm_DebugHasWarpPoint(void);
extern void PortPpuMzm_DebugGetWarpPointInfo(char* out, int outSize);
extern bool PortPpuMzm_DebugRequestWarp(void);
extern int PortPpuMzm_DebugGetDoorCount(int area);
extern int PortPpuMzm_DebugGetDoorRoom(int area, int door);
extern void PortPpuMzm_DebugGetWarpSelection(int* outArea, int* outDoor, int* outRoom);
extern void PortPpuMzm_DebugStepWarpArea(int delta);
extern void PortPpuMzm_DebugStepWarpDoor(int delta);
extern bool PortPpuMzm_DebugRequestWarpToSelection(void);
extern int PortPpuMzm_DebugRequestWarpToMapTile(int area, int tileX, int tileY);
extern void PortPpuMzm_DebugRevealAllMaps(void);
extern void PortPpuMzm_DebugRevealAreaMap(int area);
extern void PortPpuMzm_DebugMarkAreaVisited(int area);
extern void PortPpuMzm_DebugClearAreaMap(int area);
extern int  PortPpuMzm_DebugAreaMapPercent(int area);

/* Per-area STATUS map-box debug cycle: 0 actual, 1 forced-download, 2 forced-100%.
 * Read by RenderStatusView to colour the box; advanced by the touch handler. */
static uint8_t sMapDebugState[7] = { 0, 0, 0, 0, 0, 0, 0 };

/* STATUS suit rows: per-row cycle position. Only index 1 (Speed Booster) and
 * index 3 (Screw Attack) use all three steps (none / owned / always-active). */
static uint8_t sSuitCycle[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
extern void PortPpuMzm_DebugSetGod(bool on);
extern bool PortPpuMzm_DebugGetGod(void);
extern void PortPpuMzm_DebugGetEquipment(unsigned* outBeams, unsigned* outMisc);
extern void PortPpuMzm_DebugToggleBeam(unsigned bit);
extern void PortPpuMzm_DebugToggleMisc(unsigned bit);
extern void PortPpuMzm_DebugSetMisc(unsigned bit, bool on);
extern void PortPpuMzm_DebugSetForceEffect(unsigned smfBit, bool on);
extern bool PortPpuMzm_DebugGetForceEffect(unsigned smfBit);
extern void PortPpuMzm_DebugSetAllEquipment(bool on);
extern void PortPpuMzm_DebugSetAmmo(bool full);
extern void PortPpuMzm_DebugRefillAmmo(void);
extern void PortPpuMzm_DebugGetAmmoText(char* out, int outSize);

static void RenderStateView(void);

#include "ftp_server_3ds.h"
extern bool Port_Config_DebugFtpOn(void);
extern void Port_Config_SetDebugFtpOn(bool on);
static void RenderDebugToolsModal(int lang);
static bool HandleDebugToolsModalTouch(int x, int y);
static void RenderDebugWarpModal(int lang);
static void HandleDebugWarpModalTouch(int x, int y);
static void RenderDebugEquipModal(int lang);
static void HandleDebugEquipModalTouch(int x, int y);

static void DebugToolsSetMsg(const char* msg) {
    snprintf(sDebugToolsMsg, sizeof(sDebugToolsMsg), "%s", msg);
    sDebugToolsMsgUntil = sFrameCounter + 180; /* ~3s at 60Hz */
}
#endif
/* Confirmation dialog: false = enable hardcore (+restart), true = plain restart */
static bool sShowConfirmModal = false;
static bool sConfirmIsRestart = false;

/* Remap modal scroll state */
static float sRemapScrollY = 0.0f;
/* The map touch has moved past the tap radius and is panning (Port_BottomUI_HandleTouchDrag). */
static bool sMapPanStarted = false;

/* Triggers the game's soft reset flow (returns to intro/title like L+R+Start+Select).
 * GameMode is s16; GM_START_SOFT_RESET == 14. */
extern int16_t gMainGameMode;
static void TriggerGameRestart(void) {
    /* Traced so mzm-debug.log can tell this apart from a controller-combo
     * reset -- see the RESET TRACE block in Platform3DS_PollKeysIntoGba. */
    extern void Port_DebugLog(const char* msg);
    Port_DebugLog("RESET TRACE: bottom-screen RESTART button");
    gMainGameMode = 14; /* GM_START_SOFT_RESET */
}

/* Config & Display Helper Externs */
extern bool Port_Config_GetAutoHideHud(void);
extern void Port_Config_SetAutoHideHud(bool on);
extern bool Port_Config_GetHideSpoilers(void);
extern void Port_Config_SetHideSpoilers(bool on);
extern int Port_Config_GetButtonMapping(int buttonIndex);
extern void Port_Config_SetButtonMapping(int buttonIndex, int action);
extern void Port_Config_CycleButtonMapping(int buttonIndex);
extern void Port_Config_ResetButtonMappingDefault(void);
extern const char* Port_Config_GetActionName(int action, int lang);
extern int Port_Config_GetCstickMode(void);
extern void Port_Config_SetCstickMode(int mode);

/* Engine audio, for the small "tap to do what a button does" shortcuts. */
extern void SoundPlay(uint16_t sound);
#define SND_MISSILE_TOGGLE 0x85u /* SOUND_MISSILE_TOGGLE, include/constants/audio.h */

/* GBA-look top-screen effects: level 0..3 (OFF/LOW/MEDIUM/HIGH). */
extern int Port_Config_GetGbaFxGrade(void);
extern void Port_Config_SetGbaFxGrade(int level);
extern int Port_Config_GetGbaFxGrid(void);
extern void Port_Config_SetGbaFxGrid(int level);
extern int Port_Config_GetGbaFxVignette(void);
extern void Port_Config_SetGbaFxVignette(int level);
extern int Port_Config_GetFramePacing(void);
extern void Port_Config_SetFramePacing(int mode);

/* RetroAchievements Helpers */
#include "port_retroachievements_3ds.h"
#include "port_paths.h"

/* Tapping a card in the list opens a read-only detail popup over it. The
 * chosen achievement is snapshotted rather than referenced, so a background
 * list refresh (an unlock, a hardcore toggle) cannot move it out from under
 * the popup. */
static bool sShowAchDetailModal = false;
static RetroAchievementItem sAchDetail;

/* Area selector & zoom state */
static uint8_t sViewArea = 0;
static bool sFollowSamus = true;
static int sZoomLevel = 1; /* 0 = 1x (Overview), 1 = 2x (Detail), 2 = 3x (Ultra) */
static float sScrollX = 0.0f;
static float sScrollY = 0.0f;
static float sAchievementsScrollY = 0.0f;
static float sAchPacksScrollY = 0.0f;
/* Latched on a press that actually lands on the achievements/pack scrollbar
 * TRACK, and the only thing the drag path keys off afterwards. The hit test
 * used to be a bare `x >= 300` on every move event, with no vertical bound:
 * the whole right-hand column counted as the scrollbar, footer row included.
 * Tapping the sort or direction button at x >= 300 fired the button on the
 * press and then, while the finger was still down, the next move event was
 * read as a scrollbar drag at y ~= 204 -- past the end of the 48..200 track,
 * so it clamped and threw the list to the bottom. Latching on the press also
 * means a drag that starts on the thumb keeps tracking when the finger
 * wanders off the track, which the bare coordinate test got wrong the other
 * way. */
static bool sAchScrollbarDrag = false;

/* ---- Achievements list geometry -------------------------------------
 * The card height lives in ONE place because the render pass and the touch
 * pass both need it and they used to disagree: rendering laid out fixed 34px
 * cards while the hit-testing measured wrapped description text
 * (24 + lines*9 + 6, min 38, +6 gap). The scroll extent was therefore
 * computed for a taller list than was ever drawn, which is what let the
 * content scroll down past the modal and collide with the BACK button
 * (issue #32). Both passes call this now, so they cannot drift again. */
#define ACH_LIST_CLIP_Y0 48.0f
#define ACH_LIST_CLIP_Y1 200.0f
#define ACH_CARD_H 34.0f
#define ACH_SETTINGS_BTN_X 16
#define ACH_SETTINGS_BTN_W 92
#define ACH_SETTINGS_BTN_HIT(x, y) ((x) >= ACH_SETTINGS_BTN_X && (x) <= ACH_SETTINGS_BTN_X + ACH_SETTINGS_BTN_W && (y) >= 204 && (y) <= 230)

static float AchievementsContentHeight(void) {
    return (float)Port_RA_GetViewCount() * ACH_CARD_H;
}

static float AchievementsMaxScroll(void) {
    float view = ACH_LIST_CLIP_Y1 - ACH_LIST_CLIP_Y0;
    float content = AchievementsContentHeight();
    return (content > view) ? (content - view) : 0.0f;
}

#define ACH_PACK_CARD_H 40.0f

/* Touch Y on the scrollbar track -> scroll offset. The track spans the same
 * band the cards are clipped to, so it is derived from that rather than from
 * the 48.0f / 124.0f literals this was open-coded with in four places. */
static float AchScrollbarValue(int touchY, float maxScroll) {
    float trackH = ACH_LIST_CLIP_Y1 - ACH_LIST_CLIP_Y0;
    float trackY = (float)touchY - ACH_LIST_CLIP_Y0;
    if (trackY < 0.0f) trackY = 0.0f;
    if (trackY > trackH) trackY = trackH;
    return (trackY / trackH) * maxScroll;
}

static float AchPacksMaxScroll(void) {
    float view = ACH_LIST_CLIP_Y1 - ACH_LIST_CLIP_Y0;
    /* +1 for the "ALL SETS" entry that heads the chooser. */
    float content = (float)(Port_RA_GetSubsetCount() + 1u) * ACH_PACK_CARD_H;
    return (content > view) ? (content - view) : 0.0f;
}

/* A solid rect clipped to a vertical band. The card backgrounds used to be
 * drawn unclipped while only their contents (badge, status pip, title) tested
 * against the band, so a partially-scrolled card painted its full height over
 * the modal chrome below. */
static void DrawRectClipped(float x, float y, float depth, float w, float h, uint32_t color,
                            float clipY0, float clipY1) {
    float y0 = (y < clipY0) ? clipY0 : y;
    float y1 = (y + h > clipY1) ? clipY1 : (y + h);
    if (y1 <= y0) return;
    C2D_DrawRectSolid(x, y0, depth, w, y1 - y0, color);
}

/* Opens the achievements UI from the OPTIONS tab, picking the entry point:
 * the pack chooser when the game has more than one set, the flat list
 * otherwise. */
static void OpenAchievementsUi(void) {
    sAchievementsScrollY = 0.0f;
    sAchPacksScrollY = 0.0f;
    if (Port_RA_GetSubsetCount() > 1u) {
        sShowAchPacksModal = true;
        sShowAchievementsModal = false;
        sAchFromPacks = true;
    } else {
        Port_RA_SetListSubset(0);
        sShowAchPacksModal = false;
        sShowAchievementsModal = true;
        sAchFromPacks = false;
    }
}


/* BACK from the achievement list: to the pack chooser when we came in through
 * it, otherwise straight out of the modal. */
static void AchievementsBack(void) {
    sShowAchDetailModal = false;
    /* The tab is the achievements' home, so there is nothing to go back to
     * unless the list was reached through the pack chooser. */
    if (sAchFromPacks) {
        sShowAchievementsModal = false;
        sShowAchPacksModal = true;
        Port_RA_SetListSubset(0);
    }
}

static void CycleAchievementsSort(void) {
    RetroAchievementSort next = (RetroAchievementSort)((Port_RA_GetListSort() + 1) % RA_SORT_COUNT);
    Port_RA_SetListSort(next);
    sAchievementsScrollY = 0.0f; /* the old offset means nothing in a new order */
}

static void ToggleAchievementsSortDir(void) {
    Port_RA_SetListDescending(!Port_RA_GetListDescending());
    sAchievementsScrollY = 0.0f;
}

static int sLastTouchX = -1;
static int sLastTouchY = -1;
static int sTouchStartX = -1;
static int sTouchStartY = -1;
static bool sIsDragging = false;
static bool sIsTouchDragging = false;
/* Pixels a touch may wander from where it landed and still be a tap: a finger or stylus pressed
 * on the screen wobbles a few pixels on its own, and lists and the map that scrolled on every
 * one of them both moved under a tap and stopped counting it as one. Past it, the touch is a
 * drag and the content follows. */
#define TOUCH_DRAG_SLOP 6.0f

/* Pack chooser touch: same scroll/drag shape as the achievement list, with
 * row 0 reserved for the "all sets" entry. */
static void HandleAchPacksTouch(int x, int y, bool isNewTap) {
    float maxScroll = AchPacksMaxScroll();

    if (isNewTap) {
        if (ACH_SETTINGS_BTN_HIT(x, y)) {
            sShowRASettingsModal = true;
            sLastTouchX = -1;
            sLastTouchY = -1;
            return;
        }
        if (x >= 300 && (float)y >= ACH_LIST_CLIP_Y0 && (float)y < ACH_LIST_CLIP_Y1 &&
            maxScroll > 0.0f) {
            sAchScrollbarDrag = true;
            sAchPacksScrollY = AchScrollbarValue(y, maxScroll);
            sIsTouchDragging = true;
            return;
        }
        /* Picking a set happens on RELEASE, in Port_BottomUI_TouchReleased,
         * and only when the touch never turned into a drag -- the same shape
         * the remap modal uses. Opening on the press instead would fire the
         * moment a finger landed on a card to scroll the list. */
        sTouchStartX = x;
        sTouchStartY = y;
        sLastTouchX = x;
        sLastTouchY = y;
        sIsTouchDragging = false;
        return;
    }

    if (sAchScrollbarDrag && maxScroll > 0.0f) {
        sAchPacksScrollY = AchScrollbarValue(y, maxScroll);
        sIsTouchDragging = true;
    } else if (!sAchScrollbarDrag && sLastTouchX >= 0 && sLastTouchY >= 0) {
        const float total = (sTouchStartY >= 0) ? (float)(y - sTouchStartY) : 0.0f;
        if (!sIsTouchDragging && total > -TOUCH_DRAG_SLOP && total < TOUCH_DRAG_SLOP) {
            sLastTouchX = x;   /* the wobble of a tap: the release still opens the row */
            sLastTouchY = y;
            return;
        }
        float dy = (float)(sLastTouchY - y);
        sAchPacksScrollY += dy;
        if (sAchPacksScrollY < 0.0f) sAchPacksScrollY = 0.0f;
        if (sAchPacksScrollY > maxScroll) sAchPacksScrollY = maxScroll;
        sLastTouchX = x;
        sLastTouchY = y;
        sIsTouchDragging = true;
    }
}

int Port_BottomUI_GetZoom(void) { return sZoomLevel; }
void Port_BottomUI_SetZoom(int zoom) { if (zoom >= 0 && zoom <= 2) { sZoomLevel = zoom; Port_BottomUI_MarkDirty(); } }

int Port_BottomUI_GetViewArea(void) { return (int)sViewArea; }
void Port_BottomUI_SetViewArea(int area) { if (area >= 0 && area <= 6) { sViewArea = (uint8_t)area; Port_BottomUI_MarkDirty(); } }

bool Port_BottomUI_GetFollowSamus(void) { return sFollowSamus; }
void Port_BottomUI_SetFollowSamus(bool follow) { sFollowSamus = follow; Port_BottomUI_MarkDirty(); }

#define LANGUAGE_COUNT 7
static const char* Port_Config_GetLanguageDisplayName(int lang);
static void Port_Config_CycleLanguage(void);
static const char* GetAspectRatioDisplayName(int lang);
static const char* GetDisplayStyleDisplayName(int lang);
static const char* GetFpsOverlayDisplayName(int lang);
static int DispCellHit(int x, int y);

/* Cached buffer for viewing other areas */
static uint16_t sOtherAreaTiles[32 * 32];
static uint8_t sCachedOtherArea = 0xFF;

/* Language helpers (0=JP, 1=HIRA, 2=EN, 3=DE, 4=FR, 5=IT, 6=ES) */
static inline int GetLang(void) {
    if (gLanguage >= 0 && gLanguage <= 6) return (int)gLanguage;
    return 2; /* Default English */
}

bool Port_BottomUI_DebugTabVisible(void) {
#ifdef PORT_DEBUG_TOOLS_ACTIVE
    return true;
#else
    return false;
#endif
}

static const char* AreaName(uint8_t area) {
    switch (area) {
        case 0: return "BRINSTAR";
        case 1: return "KRAID";
        case 2: return "NORFAIR";
        case 3: return "RIDLEY";
        case 4: return "TOURIAN";
        case 5: return "CRATERIA";
        case 6: return "CHOZODIA";
        case 7: return "DEBUG";
        default: return "UNKNOWN";
    }
}

/* Crisp 5x7 bitmap font glyph table */
static const uint8_t* GetGlyph(char c) {
    static const uint8_t digits[10][7] = {
        { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, /* 0 */
        { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* 1 */
        { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, /* 2 */
        { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E }, /* 3 */
        { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, /* 4 */
        { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E }, /* 5 */
        { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E }, /* 6 */
        { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
        { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, /* 8 */
        { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E }, /* 9 */
    };
    static const uint8_t letters[26][7] = {
        { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* A */
        { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, /* B */
        { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, /* C */
        { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C }, /* D */
        { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, /* E */
        { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, /* F */
        { 0x0E, 0x11, 0x10, 0x13, 0x11, 0x11, 0x0F }, /* G */
        { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* H */
        { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* I */
        { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, /* J */
        { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
        { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, /* L */
        { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
        { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }, /* N */
        { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* O */
        { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, /* P */
        { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, /* Q */
        { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, /* R */
        { 0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E }, /* S */
        { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* U */
        { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, /* V */
        { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }, /* W */
        { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, /* X */
        { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, /* Y */
        { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, /* Z */
    };
    static const uint8_t colon[7]   = { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 };
    static const uint8_t dot[7]     = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
    static const uint8_t dash[7]    = { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
    static const uint8_t slash[7]   = { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 };
    static const uint8_t percent[7] = { 0x13, 0x13, 0x02, 0x04, 0x08, 0x19, 0x19 };
    static const uint8_t lbracket[7]= { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E };
    static const uint8_t rbracket[7]= { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E };
    static const uint8_t checkmark[7]={ 0x00, 0x01, 0x01, 0x02, 0x12, 0x0C, 0x00 }; /* ✓ / * */
    static const uint8_t plus[7]    = { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
    static const uint8_t equal[7]   = { 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00 };
    static const uint8_t exclam[7]  = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 };
    static const uint8_t question[7]= { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
    static const uint8_t gt[7]      = { 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10 };
    static const uint8_t lt[7]      = { 0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01 };

    static const uint8_t n_tilde[7] = { 0x1A, 0x00, 0x11, 0x19, 0x15, 0x13, 0x11 }; /* Ñ */
    static const uint8_t c_cedil[7] = { 0x0E, 0x11, 0x10, 0x10, 0x11, 0x0E, 0x04 }; /* Ç */
    static const uint8_t a_acute[7] = { 0x02, 0x04, 0x0E, 0x11, 0x1F, 0x11, 0x11 }; /* Á */
    static const uint8_t e_acute[7] = { 0x02, 0x04, 0x1F, 0x10, 0x1E, 0x10, 0x1F }; /* É */
    static const uint8_t i_acute[7] = { 0x02, 0x04, 0x0E, 0x04, 0x04, 0x04, 0x0E }; /* Í */
    static const uint8_t o_acute[7] = { 0x02, 0x04, 0x0E, 0x11, 0x11, 0x11, 0x0E }; /* Ó */
    static const uint8_t u_acute[7] = { 0x02, 0x04, 0x11, 0x11, 0x11, 0x11, 0x0E }; /* Ú */
    static const uint8_t a_umlaut[7]= { 0x0A, 0x00, 0x0E, 0x11, 0x1F, 0x11, 0x11 }; /* Ä */
    static const uint8_t o_umlaut[7]= { 0x0A, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E }; /* Ö */
    static const uint8_t u_umlaut[7]= { 0x0A, 0x00, 0x11, 0x11, 0x11, 0x11, 0x0E }; /* Ü */
    static const uint8_t eszett[7]  = { 0x1E, 0x11, 0x1E, 0x11, 0x11, 0x11, 0x1C }; /* ß */

    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= 'a' && c <= 'z') return letters[c - 'a'];
    if (c == ':') return colon;
    if (c == '.') return dot;
    if (c == '-') return dash;
    if (c == '/') return slash;
    if (c == '%') return percent;
    if (c == '[') return lbracket;
    if (c == ']') return rbracket;
    if (c == '(') return lbracket;
    if (c == ')') return rbracket;
    if (c == '*' || c == 'v' || c == '#') return checkmark;
    if (c == '+') return plus;
    if (c == '=') return equal;
    if (c == '!') return exclam;
    if (c == '?') return question;
    if (c == '>') return gt;
    if (c == '<') return lt;

    /* Single-byte ISO-8859-1 or normalized codepoints */
    switch ((uint8_t)c) {
        case 0xD1: case 0xF1: return n_tilde; /* Ñ, ñ */
        case 0xC7: case 0xE7: return c_cedil; /* Ç, ç */
        case 0xC1: case 0xE1: case 0xC0: case 0xE0: return a_acute; /* Á, á, À, à */
        case 0xC9: case 0xE9: case 0xC8: case 0xE8: case 0xCA: case 0xEA: return e_acute; /* É, é, È, è, Ê, ê */
        case 0xCD: case 0xED: case 0xCC: case 0xEC: return i_acute; /* Í, í, Ì, ì */
        case 0xD3: case 0xF3: case 0xD2: case 0xF2: return o_acute; /* Ó, ó, Ò, ò */
        case 0xDA: case 0xFA: case 0xD9: case 0xF9: return u_acute; /* Ú, ú, Ù, ù */
        case 0xC4: case 0xE4: return a_umlaut; /* Ä, ä */
        case 0xD6: case 0xF6: return o_umlaut; /* Ö, ö */
        case 0xDC: case 0xFC: return u_umlaut; /* Ü, ü */
        case 0xDF: return eszett; /* ß */
        default: break;
    }
    return NULL;
}

static const uint8_t* GetUtf8Glyph(const char** textPtr) {
    const uint8_t* s = (const uint8_t*)*textPtr;
    if (!*s) return NULL;
    if (s[0] < 0x80) {
        (*textPtr)++;
        return GetGlyph((char)s[0]);
    }
    /* 2-byte UTF-8 */
    if ((s[0] & 0xE0) == 0xC0 && s[1]) {
        uint16_t code = (uint16_t)(((s[0] & 0x1F) << 6) | (s[1] & 0x3F));
        *textPtr += 2;
        if (code >= 0x00A0 && code <= 0x00FF) {
            return GetGlyph((char)code);
        }
        return NULL;
    }
    /* 3-byte UTF-8 (e.g. CJK / Katakana / Hiragana) */
    if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
        *textPtr += 3;
        return NULL;
    }
    /* 4-byte UTF-8 */
    if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        *textPtr += 4;
        return NULL;
    }
    (*textPtr)++;
    return NULL;
}

static void DrawTextClipped(float x, float y, float scale, const char* text, uint32_t color, float clipY0, float clipY1) {
    x = (float)(int)(x + (x >= 0.0f ? 0.5f : -0.5f));
    y = (float)(int)(y + (y >= 0.0f ? 0.5f : -0.5f));
    if (y + 7.0f * scale <= clipY0 || y >= clipY1) return;
    float charW = 6.0f * scale;
    while (*text) {
        const uint8_t* glyph = GetUtf8Glyph(&text);
        if (!glyph) {
            x += charW;
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            float py = y + (float)row * scale;
            if (py < clipY0 || py + scale > clipY1) continue;
            uint8_t rowVal = glyph[row];
            for (int col = 0; col < 5;) {
                if ((rowVal & (1u << (4 - col))) == 0) {
                    ++col;
                    continue;
                }
                int end = col + 1;
                while (end < 5 && (rowVal & (1u << (4 - end))) != 0) ++end;
                C2D_DrawRectSolid(x + (float)col * scale, py, 0.85f,
                                  (float)(end - col) * scale, scale, color);
                col = end;
            }
        }
        x += charW;
    }
}

static void DrawTextMaxWClipped(float x, float y, float scale, const char* text, uint32_t color, float clipY0, float clipY1, float maxW) {
    x = (float)(int)(x + (x >= 0.0f ? 0.5f : -0.5f));
    y = (float)(int)(y + (y >= 0.0f ? 0.5f : -0.5f));
    if (y + 7.0f * scale <= clipY0 || y >= clipY1) return;
    float charW = 6.0f * scale;
    float startX = x;
    while (*text) {
        if (x + charW > startX + maxW) break;
        const uint8_t* glyph = GetUtf8Glyph(&text);
        if (!glyph) {
            x += charW;
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            float py = y + (float)row * scale;
            if (py < clipY0 || py + scale > clipY1) continue;
            uint8_t rowVal = glyph[row];
            for (int col = 0; col < 5;) {
                if ((rowVal & (1u << (4 - col))) == 0) {
                    ++col;
                    continue;
                }
                int end = col + 1;
                while (end < 5 && (rowVal & (1u << (4 - end))) != 0) ++end;
                C2D_DrawRectSolid(x + (float)col * scale, py, 0.85f,
                                  (float)(end - col) * scale, scale, color);
                col = end;
            }
        }
        x += charW;
    }
}


static void DrawWrappedTextClipped(float x, float y, float scale, const char* text, float maxWidth, float lineHeight, uint32_t color, float clipY0, float clipY1) {
    if (!text || !*text) return;
    float charW = 6.0f * scale;
    int maxCharsPerLine = (int)(maxWidth / charW);
    if (maxCharsPerLine < 10) maxCharsPerLine = 10;

    char lineBuf[128];
    int lineBufLen = 0;
    float curY = y;
    const char* ptr = text;

    while (*ptr) {
        const char* wordStart = ptr;
        while (*ptr && *ptr != ' ') ++ptr;
        int wordLen = (int)(ptr - wordStart);

        if (lineBufLen == 0) {
            if (wordLen < (int)sizeof(lineBuf) - 1) {
                memcpy(lineBuf, wordStart, wordLen);
                lineBufLen = wordLen;
                lineBuf[lineBufLen] = '\0';
            }
        } else if (lineBufLen + 1 + wordLen <= maxCharsPerLine && lineBufLen + 1 + wordLen < (int)sizeof(lineBuf) - 1) {
            lineBuf[lineBufLen++] = ' ';
            memcpy(lineBuf + lineBufLen, wordStart, wordLen);
            lineBufLen += wordLen;
            lineBuf[lineBufLen] = '\0';
        } else {
            /* flush current line */
            DrawTextClipped(x, curY, scale, lineBuf, color, clipY0, clipY1);
            curY += lineHeight;
            memcpy(lineBuf, wordStart, wordLen);
            lineBufLen = wordLen;
            lineBuf[lineBufLen] = '\0';
        }
        while (*ptr == ' ') ++ptr;
    }
    if (lineBufLen > 0) {
        DrawTextClipped(x, curY, scale, lineBuf, color, clipY0, clipY1);
    }
}

static void DrawText(float x, float y, float scale, const char* text, uint32_t color) {
    DrawTextClipped(x, y, scale, text, color, -1000.0f, 1000.0f);
}

static int Utf8CharCount(const char* text) {
    if (!text) return 0;
    int count = 0;
    while (*text) {
        GetUtf8Glyph(&text);
        ++count;
    }
    return count;
}

static void DrawTextCentered(float cx, float y, float scale, const char* text, uint32_t color) {
    float length = (float)Utf8CharCount(text) * 6.0f * scale;
    DrawText(cx - (length / 2.0f), y, scale, text, color);
}

static void DrawTextCenteredClipped(float cx, float y, float scale, const char* text, uint32_t color, float clipY0, float clipY1) {
    float length = (float)Utf8CharCount(text) * 6.0f * scale;
    DrawTextClipped(cx - (length / 2.0f), y, scale, text, color, clipY0, clipY1);
}

/* --- Shared button chrome ------------------------------------------------
 * One look for every tappable box on the bottom screen: a 1px border, a
 * filled body, a 1px top bevel, and a short press flash keyed on where the
 * last stylus tap landed (Port_BottomUI_NoteTap, called from the touch
 * handler). DrawButtonBox draws just the frame and returns whether it is
 * flashing, so a multi-line caller can nudge its own text; DrawButton also
 * centres a single label on both axes and shrinks it to fit the width. */
static int sTapFxX = -10000, sTapFxY = 0;
static uint32_t sTapFxFrame = 0;
#define BTN_FX_FRAMES 12u

static void Port_BottomUI_NoteTap(int x, int y) {
    sTapFxX = x; sTapFxY = y; sTapFxFrame = sFrameCounter;
}

/* True for a few frames after a tap that landed inside this rect. */
static bool BtnTapFlash(float x, float y, float w, float h) {
    return (uint32_t)(sFrameCounter - sTapFxFrame) < BTN_FX_FRAMES &&
           (float)sTapFxX >= x && (float)sTapFxX < x + w &&
           (float)sTapFxY >= y && (float)sTapFxY < y + h;
}

static bool DrawButtonBox(float x, float y, float w, float h,
                          uint32_t bodyCol, uint32_t borderCol) {
    bool pressed = BtnTapFlash(x, y, w, h);
    uint32_t body = bodyCol, border = borderCol, bevel;
    if (pressed) {
        body   = C2D_Color32(90, 150, 220, 255);
        border = C2D_Color32(210, 235, 255, 255);
        bevel  = border;
    } else {
        bevel = C2D_Color32(90, 130, 180, 255);
    }
    C2D_DrawRectSolid(x, y, 0.90f, w, h, border);
    C2D_DrawRectSolid(x + 1.0f, y + 1.0f, 0.91f, w - 2.0f, h - 2.0f, body);
    C2D_DrawRectSolid(x + 1.0f, y + 1.0f, 0.92f, w - 2.0f, 1.0f, bevel);
    return pressed;
}

static void DrawButton(float x, float y, float w, float h, const char* label,
                       uint32_t textCol, uint32_t bodyCol, uint32_t borderCol) {
    bool pressed = DrawButtonBox(x, y, w, h, bodyCol, borderCol);
    if (!label || !*label) return;
    float scale = 1.0f;
    float tw = (float)Utf8CharCount(label) * 6.0f;
    float avail = w - 4.0f;
    if (tw > avail && tw > 0.0f) {
        scale = avail / tw;
        if (scale < 0.5f) scale = 0.5f;
    }
    float ty = y + (h - 7.0f * scale) * 0.5f + (pressed ? 1.0f : 0.0f);
    DrawTextCentered(x + w * 0.5f, ty, scale, label, textCol);
}

/* Default palette for DrawButton/DrawButtonBox: the same blue the footer
 * CLOSE/BACK boxes have always used. */
#define BTN_BLUE_BODY   C2D_Color32(20, 70, 130, 255)
#define BTN_BLUE_BORDER C2D_Color32(70, 130, 210, 255)

/* Small centred glyph for play/stop toggle controls: a right-pointing
 * triangle when idle, a filled square when something is playing. */
static void DrawActionGlyph(float cx, float cy, bool playing, uint32_t col) {
    float x = floorf(cx), y = floorf(cy);
    if (playing) {
        C2D_DrawRectSolid(x - 3.0f, y - 3.0f, 0.95f, 6.0f, 6.0f, col);
    } else {
        C2D_DrawTriangle(x - 3.0f, y - 4.0f, col, x - 3.0f, y + 4.0f, col,
                         x + 4.0f, y, col, 0.95f);
    }
}

static inline uint32_t Bgr555ToRgba8(uint16_t bgr, bool transparent) {
    if (transparent) return 0;
    uint32_t r = (bgr & 0x1F) * 255 / 31;
    uint32_t g = ((bgr >> 5) & 0x1F) * 255 / 31;
    uint32_t b = ((bgr >> 10) & 0x1F) * 255 / 31;
    return C2D_Color32(r, g, b, 255);
}

static float GetTileSizeForZoom(int zoom) {
    switch (zoom) {
        case 0: return 5.5f;   /* 1x Overview: 32x32 = 176x176 px */
        case 1: return 12.0f;  /* 2x Detail: 32x32 = 384x384 px */
        case 2: return 18.0f;  /* 3x Ultra: 32x32 = 576x576 px */
        default: return 12.0f;
    }
}

/* Inverse of the map canvas' draw transform in RenderMapView -- screen
 * pixel to map tile. Kept next to CenterMapOnTile (its forward twin) so the
 * two stay in sync; the canvas origin/size constants are the ones that
 * function hardcodes. Returns false when the tap falls outside the 32x32
 * map. */
static bool MapTileAtTouch(int x, int y, int* outTileX, int* outTileY) {
    const float canvasX = 4.0f, canvasY = 48.0f, canvasW = 312.0f, canvasH = 188.0f;
    float tileSize = GetTileSizeForZoom(sZoomLevel);
    float startDrawX, startDrawY;
    if (sZoomLevel == 0) {
        startDrawX = canvasX + (canvasW - 32.0f * tileSize) / 2.0f;
        startDrawY = canvasY + (canvasH - 32.0f * tileSize) / 2.0f;
    } else {
        startDrawX = canvasX - sScrollX;
        startDrawY = canvasY - sScrollY;
    }
    int tx = (int)(((float)x - startDrawX) / tileSize);
    int ty = (int)(((float)y - startDrawY) / tileSize);
    if (tx < 0 || tx > 31 || ty < 0 || ty > 31) return false;
    *outTileX = tx;
    *outTileY = ty;
    return true;
}

static void CenterMapOnTile(uint8_t tileX, uint8_t tileY) {
    float tileSize = GetTileSizeForZoom(sZoomLevel);
    const float viewW = 312.0f;
    const float viewH = 188.0f;

    if (sZoomLevel == 0) {
        sScrollX = 0.0f;
        sScrollY = 0.0f;
    } else {
        float focusX = (float)tileX * tileSize + tileSize / 2.0f;
        float focusY = (float)tileY * tileSize + tileSize / 2.0f;
        sScrollX = focusX - (viewW / 2.0f);
        sScrollY = focusY - (viewH / 2.0f);

        float maxScrollX = (32.0f * tileSize) - viewW;
        float maxScrollY = (32.0f * tileSize) - viewH;
        if (maxScrollX < 0.0f) maxScrollX = 0.0f;
        if (maxScrollY < 0.0f) maxScrollY = 0.0f;

        if (sScrollX < 0.0f) sScrollX = 0.0f;
        if (sScrollX > maxScrollX) sScrollX = maxScrollX;
        if (sScrollY < 0.0f) sScrollY = 0.0f;
        if (sScrollY > maxScrollY) sScrollY = maxScrollY;
    }
}

void Port_BottomUI_Init(void) {
    if (!sPtmuReady && R_SUCCEEDED(ptmuInit())) sPtmuReady = true;
    sCurrentTab = BOTTOM_TAB_MAP;
    sFrameCounter = 0;
    sViewArea = 0;
    sFollowSamus = true;
    sZoomLevel = 1;
    sScrollX = 0.0f;
    sScrollY = 0.0f;
    sLastTouchX = -1;
    sLastTouchY = -1;
    sIsDragging = false;
    sCachedOtherArea = 0xFF;
}

void Port_BottomUI_SetTab(PortBottomTab tab) {
    if (tab < BOTTOM_TAB_COUNT) {
        sCurrentTab = tab;
        Port_BottomUI_MarkDirty();
    }
}

PortBottomTab Port_BottomUI_GetTab(void) {
    return sCurrentTab;
}

/* Check active Chozo target */
static bool GetActiveChozoTarget(uint8_t* outArea, uint8_t* outX, uint8_t* outY) {
    if (!p_sChozoStatueTargets || !ChozoStatueHintCheckTargetIsActivated) return false;
    for (uint8_t i = 0; i < 16; ++i) {
        /* Flame targets (startIcon >= 8) are the post-boss statue openings:
         * retail marks those with just the flame at the opened statue, no
         * crosshair and no route -- handled separately below. Only the
         * item-hint targets (startIcon == TARGET_OAM_ID_TARGET) drive the
         * objective crosshair and the elevator route arrows. */
        if ((*p_sChozoStatueTargets)[i].startIcon >= 8) continue;
        if (ChozoStatueHintCheckTargetIsActivated(i) > 0) {
            *outArea = (*p_sChozoStatueTargets)[i].targetArea;
            *outX = (*p_sChozoStatueTargets)[i].targetX;
            *outY = (*p_sChozoStatueTargets)[i].targetY;
            return true;
        }
    }
    return false;
}

extern void Port_Config_Save(void);

#ifdef PORT_DEBUG_TOOLS_ACTIVE
/* Equipment bits per row of the STATUS page's two columns, in draw order --
 * mirrors the beams[]/suits[] tables in RenderStatusView. Used by the debug
 * tap-to-toggle in Port_BottomUI_HandleTouchDrag. */
static const uint8_t kStatusBeamBits[6] = { 1u<<0, 1u<<1, 1u<<2, 1u<<3, 1u<<4, 1u<<7 };
static const uint8_t kStatusSuitBits[8] = { 1u<<0, 1u<<1, 1u<<2, 1u<<3, 1u<<4, 1u<<5, 1u<<6, 1u<<7 };
static bool sDebugStatsFull = false; /* the max<->min button's toggle state */

/* Two small icon buttons to the right of the STATUS title (debug only):
 *   0: vida+municion a tope <-> al minimo (up/down arrows)
 *   1: god mode toggle (filled/tinted when on)
 * Geometry shared with the touch handler. */
#define STATUS_BTN_Y  27
#define STATUS_BTN_W  16
#define STATUS_BTN_H  14
static const float kStatusBtnX[2] = { 274.0f, 294.0f };

static int DebugStatusBtnHit(int x, int y) {
    if (y < STATUS_BTN_Y || y > STATUS_BTN_Y + STATUS_BTN_H) return -1;
    for (int i = 0; i < 2; ++i)
        if ((float)x >= kStatusBtnX[i] && (float)x <= kStatusBtnX[i] + STATUS_BTN_W) return i;
    return -1;
}

static void DrawStatusDebugButtons(void) {
    const bool god = PortPpuMzm_DebugGetGod();
    for (int i = 0; i < 2; ++i) {
        float bx = kStatusBtnX[i], by = (float)STATUS_BTN_Y;
        bool on = (i == 0) ? sDebugStatsFull : god;
        C2D_DrawRectSolid(bx, by, 0.9f, (float)STATUS_BTN_W, (float)STATUS_BTN_H,
                          on ? C2D_Color32(110, 85, 20, 255) : C2D_Color32(24, 34, 52, 255));
        C2D_DrawRectSolid(bx, by, 0.91f, (float)STATUS_BTN_W, 1.0f, C2D_Color32(60, 90, 140, 255));
        float mx = bx + STATUS_BTN_W * 0.5f, my = by + STATUS_BTN_H * 0.5f;
        uint32_t c = on ? C2D_Color32(255, 220, 90, 255) : C2D_Color32(150, 190, 230, 255);
        if (i == 0) {
            C2D_DrawTriangle(mx - 4.0f, my - 1.5f, c, mx + 4.0f, my - 1.5f, c, mx, my - 6.0f, c, 0.92f);
            C2D_DrawTriangle(mx - 4.0f, my + 1.5f, c, mx + 4.0f, my + 1.5f, c, mx, my + 6.0f, c, 0.92f);
        } else {
            C2D_DrawTriangle(mx, my - 6.0f, c, mx - 6.0f, my, c, mx + 6.0f, my, c, 0.92f);
            C2D_DrawTriangle(mx, my + 6.0f, c, mx - 6.0f, my, c, mx + 6.0f, my, c, 0.92f);
        }
    }
}
#endif /* PORT_DEBUG_TOOLS_ACTIVE */

static void HandleStateTouch(int x, int y, bool isNewTap);
static void StateTapReleased(void);
static void CloseStateModal(void);

void Port_BottomUI_HandleTouchDrag(int x, int y, bool isNewTap) {
    /* Any stylus contact can move something (pan, button, modal); redraw the
     * next frame instead of waiting for the throttle. */
    Port_BottomUI_MarkDirty();

    /* Remember where a fresh tap landed so any button under it flashes for a
     * few frames (DrawButtonBox). Purely cosmetic -- the hit tests below are
     * unchanged. */
    if (isNewTap) Port_BottomUI_NoteTap(x, y);

    /* While the SD card is being read or written the "wait" message is up and
     * nothing else can be touched (a thumbnail read is too short to matter). */
    if (Port_SaveState_IsBusy() && Port_SaveState_CurrentJob() != PORT_SS_JOB_THUMB) return;

    /* The install-only FTP server's CANCEL, over everything. */
    if (FtpServer_InstallOnly() && x >= DEVFTP_X && x < DEVFTP_X + DEVFTP_W && y >= DEVFTP_Y && y < DEVFTP_Y + DEVFTP_H) {
        if (isNewTap) {
            FtpServer_Cancel();
            Port_BottomUI_MarkDirty();
        }
        return;
    }

    /* The update prompt is modal over every tab: it swallows all touches. */
    if (Port_Updater_GetPrompt() != UPDATER_PROMPT_NONE && !sShowNotesModal) {
        if (isNewTap) {
            const bool isError = (Port_Updater_GetPrompt() == UPDATER_PROMPT_ERROR);
            const bool yes = (y >= UPD_BTN_Y0 && y <= UPD_BTN_Y1) &&
                             (isError ? (x >= 116 && x <= 204) : (x >= UPD_YES_X0 && x <= UPD_YES_X1));
            const bool no = (x >= UPD_NO_X0 && x <= UPD_NO_X1 && y >= UPD_BTN_Y0 && y <= UPD_BTN_Y1);
            if (yes || no) Port_Updater_AnswerPrompt(yes);
            else if (Port_Updater_GetPrompt() == UPDATER_PROMPT_ASK_INSTALL &&
                     x >= UPD_YES_X0 && x <= UPD_NO_X1 && y >= UPD_NOTES_Y0 && y <= UPD_NOTES_Y1) {
                char probe[2];
                if (Port_Updater_CopyNotes(probe, sizeof(probe)) > 0) {
                    /* The viewer lives on the OPTIONS tab; the prompt steps
                     * aside while it is open and comes back on CLOSE. */
                    sCurrentTab = BOTTOM_TAB_OPTIONS;
                    OpenNotesModal();
                }
            }
        }
        return;
    }

    /* Top navigation bar: icon tabs, left-aligned (Y: 2 to 24). */
    if (y >= 2 && y <= 24) {
        if (isNewTap) {
            PortBottomTab prevTab = sCurrentTab;
            BottomTabSlot slots[BOTTOM_TAB_SLOT_MAX];
            int n = BottomTabLayout(slots);
            for (int i = 0; i < n; ++i) {
                if ((float)x >= slots[i].x && (float)x <= slots[i].x + slots[i].w) {
                    sCurrentTab = slots[i].tab;
                    break;
                }
            }
            if (sCurrentTab != prevTab) Port_Config_Save();
            if (sCurrentTab != prevTab) CloseStateModal();
            if (sCurrentTab != prevTab) sShowRASettingsModal = false;
        }
        sLastTouchX = -1;
        sLastTouchY = -1;
        sIsDragging = false;
        return;
    }

    if (sCurrentTab == BOTTOM_TAB_MAP) {
        /* Collectibles breakdown overlays the whole map -- only its CLOSE
         * button dismisses it; all other taps are swallowed. */
        if (isNewTap && sShowCollectiblesModal) {
            if (x >= 100 && x <= 220 && y >= 204 && y <= 228) {
                sShowCollectiblesModal = false;
                sCollectiblesHighlightArea = -1;
            }
            return;
        }

        /* Map Subheader controls (Y: 26 to 46) */
        if (y >= 26 && y <= 46) {
            if (isNewTap) {
                /* Previous Area [ < ] */
                if (x >= 4 && x <= 20) {
                    sFollowSamus = false;
                    sViewArea = (sViewArea == 0) ? 6 : (sViewArea - 1);
                    CenterMapOnTile(16, 16);
                    Port_Config_Save();
                }
                /* Area Title Box -> toggle back to current area */
                else if (x > 20 && x < 92) {
                    sFollowSamus = true;
                    sViewArea = gCurrentArea;
                    CenterMapOnTile(gMinimapX, gMinimapY);
                    Port_Config_Save();
                }
                /* Next Area [ > ] */
                else if (x >= 92 && x <= 110) {
                    sFollowSamus = false;
                    sViewArea = (sViewArea + 1) % 7;
                    CenterMapOnTile(16, 16);
                    Port_Config_Save();
                }
                /* Area items badge -> collectibles breakdown for this area */
                else if (x >= 112 && x <= 156) {
                    sShowCollectiblesModal = true;
                    sCollectiblesHighlightArea = (int)sViewArea;
                }
                /* Zoom Button [ 1X / 2X / 3X ] */
                else if (x >= 156 && x <= 196) {
                    sZoomLevel = (sZoomLevel + 1) % 3;
                    if (sFollowSamus && gCurrentArea < 7) {
                        CenterMapOnTile(gMinimapX, gMinimapY);
                    } else {
                        CenterMapOnTile(16, 16);
                    }
                    Port_Config_Save();
                }
                /* Center on Samus Button [ SAMUS ] */
                else if (x >= 198 && x <= 250) {
                    sFollowSamus = true;
                    sViewArea = gCurrentArea;
                    CenterMapOnTile(gMinimapX, gMinimapY);
                    Port_Config_Save();
                }
                /* Target Button [ ! OBJ / COORDS ] */
                else if (x >= 252 && x <= 316) {
                    uint8_t tArea = 0, tX = 0, tY = 0;
                    if (GetActiveChozoTarget(&tArea, &tX, &tY)) {
                        sFollowSamus = false;
                        sViewArea = tArea;
                        CenterMapOnTile(tX, tY);
                        Port_Config_Save();
                    }
                }
            }
            sLastTouchX = -1;
            sLastTouchY = -1;
            sIsDragging = false;
            return;
        }

        /* Map Canvas Dragging Area (Y: 48 to 236, X: 4 to 316) */
        if (y >= 48 && y <= 236 && x >= 4 && x <= 316) {
#ifdef PORT_DEBUG_TOOLS_ACTIVE
            /* Persistent WARP button tap toggle (bottom right of map canvas;
             * kept in sync with warpBtn* in RenderMapView -- 58x16 flush to
             * the 312/232 corner, with a couple px of slack). */
            if (isNewTap && x >= 252 && x <= 312 && y >= 214 && y <= 232) {
                sDebugMapWarpArmed = !sDebugMapWarpArmed;
                sLastTouchX = -1;
                sLastTouchY = -1;
                sTouchStartX = -1;
                sTouchStartY = -1;
                sIsDragging = false;
                return;
            }
            /* Armed tap-to-warp. The warp fires on RELEASE in
             * Port_BottomUI_TouchReleased, and only if the finger barely moved. */
            if (sDebugMapWarpArmed) {
                if (isNewTap) {
                    sTouchStartX = x;
                    sTouchStartY = y;
                    sDebugMapWarpDevSq = 0;
                } else if (sTouchStartX >= 0) {
                    /* Track the WORST deviation over the whole touch, not
                     * just where the stylus happened to be at release: a
                     * pan that drifts out and comes back is still a pan. */
                    int dx = x - sTouchStartX;
                    int dy = y - sTouchStartY;
                    int devSq = dx * dx + dy * dy;
                    if (devSq > sDebugMapWarpDevSq) sDebugMapWarpDevSq = devSq;
                }
            }
#endif
            if (isNewTap) {
                sTouchStartX = x;
                sTouchStartY = y;
                sMapPanStarted = false;
            }
            if (sZoomLevel > 0) {
                /* A touch only pans the map once it has left a small circle around where it
                 * landed: before that it is a tap, and the map must not move under it (the warp
                 * looks up the tile under the first touch point when the finger lifts). While
                 * the warp is armed the circle is the warp's own tap radius (20 px). */
                if (!isNewTap && !sMapPanStarted && sTouchStartX >= 0) {
                    float slop = TOUCH_DRAG_SLOP;
#ifdef PORT_DEBUG_TOOLS_ACTIVE
                    if (sDebugMapWarpArmed) slop = 20.0f;
#endif
                    const float ddx = (float)(x - sTouchStartX), ddy = (float)(y - sTouchStartY);
                    if (ddx * ddx + ddy * ddy > slop * slop) sMapPanStarted = true;
                    sLastTouchX = x;
                    sLastTouchY = y;
                } else if (!isNewTap && sMapPanStarted && sLastTouchX >= 0 && sLastTouchY >= 0) {
                    float dx = (float)(sLastTouchX - x);
                    float dy = (float)(sLastTouchY - y);
                    if (dx != 0.0f || dy != 0.0f) {
                        sFollowSamus = false;
                    }
                    sScrollX += dx;
                    sScrollY += dy;

                    float tileSize = GetTileSizeForZoom(sZoomLevel);
                    float maxScrollX = (32.0f * tileSize) - 312.0f;
                    float maxScrollY = (32.0f * tileSize) - 188.0f;
                    if (maxScrollX < 0.0f) maxScrollX = 0.0f;
                    if (maxScrollY < 0.0f) maxScrollY = 0.0f;

                    if (sScrollX < 0.0f) sScrollX = 0.0f;
                    if (sScrollX > maxScrollX) sScrollX = maxScrollX;
                    if (sScrollY < 0.0f) sScrollY = 0.0f;
                    if (sScrollY > maxScrollY) sScrollY = maxScrollY;

                    sIsDragging = true;
                }
                sLastTouchX = x;
                sLastTouchY = y;
            }
            return;
        }
    }

    /* Status Tab Interactive Taps (Collectibles breakdown modal toggle) */
#ifdef PORT_DEBUG_TOOLS_ACTIVE
    if (sCurrentTab == BOTTOM_TAB_DEBUG) {
        if (isNewTap) {
            if (sShowDebugToolsModal) {
                HandleDebugToolsModalTouch(x, y);
            } else if (sShowDebugWarpModal) {
                HandleDebugWarpModalTouch(x, y);
            } else if (sShowDebugEquipModal) {
                HandleDebugEquipModalTouch(x, y);
            } else if (sShowSoundTestModal) {
                HandleSoundTestModalTouch(x, y);
            } else if (x >= 16 && x <= 304 && y >= 198 && y <= 224) {
                sShowDebugToolsModal = true;
            }
        }
        return;
    }
#endif

    if (sCurrentTab == BOTTOM_TAB_STATE) {
        HandleStateTouch(x, y, isNewTap);
        return;
    }

    if (sCurrentTab == BOTTOM_TAB_STATUS && isNewTap) {
        if (sShowCollectiblesModal) {
            /* Only the CLOSE button dismisses it; every other tap is swallowed
             * so the status view behind it does not react. */
            if (x >= 100 && x <= 220 && y >= 204 && y <= 228) {
                sShowCollectiblesModal = false;
                sCollectiblesHighlightArea = -1;
            }
            return;
        }

        /* Tap the missile / super-missile cell in the header card to switch
         * the selected ammo, same as the SELECT button (SamusSetHighlightedWeapon
         * only re-derives it when one type is empty). */
        if (y >= 48 && y <= 74) {
            if (x >= 86 && x <= 160 && gEquipment.maxMissiles > 0 &&
                gSamusWeaponInfo.missilesSelected != 0) {
                gSamusWeaponInfo.missilesSelected = 0;
                SoundPlay(SND_MISSILE_TOGGLE);
                Port_BottomUI_MarkDirty();
                return;
            }
            if (x >= 162 && x <= 236 && gEquipment.maxSuperMissiles > 0 &&
                gSamusWeaponInfo.missilesSelected == 0) {
                gSamusWeaponInfo.missilesSelected = 1;
                SoundPlay(SND_MISSILE_TOGGLE);
                Port_BottomUI_MarkDirty();
                return;
            }
        }
#ifdef PORT_DEBUG_TOOLS_ACTIVE
        if (Port_BottomUI_DebugTabVisible()) {
            /* Title-bar icon buttons */
            int btn = DebugStatusBtnHit(x, y);
            if (btn == 0) {
                sDebugStatsFull = !sDebugStatsFull;
                PortPpuMzm_DebugSetAmmo(sDebugStatsFull);
                Port_BottomUI_MarkDirty();
                return;
            }
            if (btn == 1) {
                bool g = !PortPpuMzm_DebugGetGod();
                PortPpuMzm_DebugSetGod(g);
                Port_BottomUI_MarkDirty();
                return;
            }
            /* Beams & bombs column: rows at py = 92 + i*11, x 8..156 */
            if (x >= 8 && x <= 156) {
                for (int i = 0; i < 6; ++i) {
                    int py = 92 + i * 11;
                    if (y >= py && y <= py + 11) {
                        PortPpuMzm_DebugToggleBeam(kStatusBeamBits[i]);
                        Port_BottomUI_MarkDirty();
                        return;
                    }
                }
            }
            /* Suits & movement column: rows at py = 91 + i*9, x 164..312 */
            if (x >= 164 && x <= 312) {
                for (int i = 0; i < 8; ++i) {
                    int py = 91 + i * 9;
                    if (y >= py && y <= py + 9) {
                        /* Speed Booster (i=1) and Screw Attack (i=3) get a
                         * 3-state cycle: none -> owned -> always-active.
                         * Every other row is a plain owned/not toggle. */
                        if (i == 1 || i == 3) {
                            unsigned smf = kStatusSuitBits[i];
                            uint8_t* st = &sSuitCycle[i];
                            *st = (uint8_t)((*st + 1) % 3);
                            PortPpuMzm_DebugSetMisc(smf, *st != 0);
                            PortPpuMzm_DebugSetForceEffect(smf, *st == 2);
                        } else {
                            PortPpuMzm_DebugToggleMisc(kStatusSuitBits[i]);
                        }
                        Port_BottomUI_MarkDirty();
                        return;
                    }
                }
            }
            /* Downloaded-maps card: tapping an area box cycles it
             *   nothing -> downloaded -> fully visited -> nothing
             * (1st tap = get the map, 2nd = mark the whole area explored).
             * The MAP tab's cache of the last non-current area is now stale. */
            {
                int hit = -1;
                if (y >= 188 && y <= 206) {
                    for (int i = 0; i < 4; ++i) { int bx = 14 + i * 73;
                        if (x >= bx && x <= bx + 68) { hit = i; break; } }
                } else if (y >= 210 && y <= 228) {
                    for (int i = 4; i < 7; ++i) { int bx = 14 + (i - 4) * 98;
                        if (x >= bx && x <= bx + 92) { hit = i; break; } }
                }
                if (hit >= 0) {
                    /* Unconditional 3-state cycle:
                     *   0 actual   (restore real state)
                     *   1 revealed (force downloaded / outline)
                     *   2 marked   (force 100% visited)
                     * -> back to 0. No "wiped" / "hidden" step. */
                    uint8_t* st = &sMapDebugState[hit];
                    *st = (uint8_t)((*st + 1) % 3);
                    if (*st == 1)      PortPpuMzm_DebugRevealAreaMap(hit);
                    else if (*st == 2) PortPpuMzm_DebugMarkAreaVisited(hit);
                    else               PortPpuMzm_DebugClearAreaMap(hit);
                    sCachedOtherArea = 0xFF;
                    Port_BottomUI_MarkDirty();
                    return;
                }
            }
        }
#endif
        if (x >= 180 && x <= 310 && y >= 166 && y <= 186) {
            sShowCollectiblesModal = true;
            sCollectiblesHighlightArea = -1;
            return;
        }
    }

    /* RA settings: a full view of the ACHIEVEMENTS tab (opened by the SETTINGS
     * button on the list / set chooser footer). */
    if (sCurrentTab == BOTTOM_TAB_ACHIEVEMENTS && sShowRASettingsModal) {
        if (isNewTap) {
            if (x >= 100 && x <= 220 && y >= 204 && y <= 230) {
                sShowRASettingsModal = false;
            } else if (x >= 10 && x <= 308) {
                if (y >= 56 && y <= 80) {
                    /* Login prompt via swkbd */
                    Port_RA_PromptLogin();
                    Port_Config_Save();
                } else if (y >= 84 && y <= 108) {
                    /* Enable / Disable toggle */
                    bool cur = Port_RA_IsEnabled();
                    Port_RA_SetEnabled(!cur);
                    Port_Config_Save();
                } else if (y >= 112 && y <= 136) {
                    /* Notification screen row: PREVIEW button on the
                     * right (fires a sample toast + jingle), rest of the
                     * row toggles bottom <-> top. (Slot was the Hardcore
                     * Mode toggle; hardcore is force-off now -- see
                     * Port_RA_HardcoreAllowed.) */
                    if (x >= 272) {
                        Port_RA_ShowPreviewToast();
                    } else {
                        Port_RA_SetNotifyOnTopScreen(!Port_RA_GetNotifyOnTopScreen());
                        Port_Config_Save();
                    }
                } else if (y >= 140 && y <= 164) {
                    /* Sound Notification toggle */
                    bool snd = Port_RA_GetNotificationSound();
                    Port_RA_SetNotificationSound(!snd);
                    Port_Config_Save();
                }
            }
        }
        return;
    }

    /* Modal: achievement detail popup. Sits on top of the list; only its BACK
     * button is interactive, everything else is swallowed so the list behind
     * it cannot scroll. */
    if (sCurrentTab == BOTTOM_TAB_ACHIEVEMENTS && sShowAchDetailModal) {
        if (isNewTap && x >= 100 && x <= 220 && y >= 206 && y <= 230) {
            sShowAchDetailModal = false;
        }
        return;
    }

    /* Modal: pack chooser (only reachable when the game has >1 set) */
    if (sCurrentTab == BOTTOM_TAB_ACHIEVEMENTS && sShowAchPacksModal) {
        HandleAchPacksTouch(x, y, isNewTap);
        return;
    }

    /* Modal: Achievements View Scrolling & Close */
    if (sCurrentTab == BOTTOM_TAB_ACHIEVEMENTS && sShowAchievementsModal) {
        float maxAchScroll = AchievementsMaxScroll();

        if (isNewTap) {
            if (ACH_SETTINGS_BTN_HIT(x, y)) {
                sShowRASettingsModal = true;
                sLastTouchX = -1;
                sLastTouchY = -1;
                return;
            }
            if (x >= 112 && x <= 204 && y >= 204 && y <= 230 && sAchFromPacks) {
                AchievementsBack();
                sLastTouchX = -1;
                sLastTouchY = -1;
                return;
            }
            if (x >= 206 && x <= 286 && y >= 204 && y <= 230) {
                CycleAchievementsSort();
                sLastTouchX = -1;
                sLastTouchY = -1;
                return;
            }
            if (x >= 288 && x <= 306 && y >= 204 && y <= 230) {
                ToggleAchievementsSortDir();
                sLastTouchX = -1;
                sLastTouchY = -1;
                return;
            }
            /* Scrollbar press: on the track only -- see sAchScrollbarDrag. */
            if (x >= 300 && (float)y >= ACH_LIST_CLIP_Y0 && (float)y < ACH_LIST_CLIP_Y1 &&
                maxAchScroll > 0.0f) {
                sAchScrollbarDrag = true;
                sAchievementsScrollY = AchScrollbarValue(y, maxAchScroll);
                sIsTouchDragging = true;
            } else {
                sTouchStartX = x;
                sTouchStartY = y;
                sLastTouchX = x;
                sLastTouchY = y;
                sIsTouchDragging = false;
            }
        } else if (sAchScrollbarDrag && maxAchScroll > 0.0f) {
            /* Dragging the scrollbar, latched on the press */
            sAchievementsScrollY = AchScrollbarValue(y, maxAchScroll);
            sIsTouchDragging = true;
        } else if (!sAchScrollbarDrag && sLastTouchX >= 0 && sLastTouchY >= 0) {
            /* Dead zone: a stylus press wobbles a few pixels before it lifts,
             * and treating that as a drag was eating every tap meant to open a
             * card. Only start scrolling once the finger has travelled past a
             * small threshold from where it first landed; below that, swallow
             * the movement so the release still counts as a tap. */
            const float DRAG_SLOP = 6.0f;
            float totalDy = (sTouchStartY >= 0) ? (float)(y - sTouchStartY) : 0.0f;
            if (!sIsTouchDragging && (totalDy > -DRAG_SLOP && totalDy < DRAG_SLOP)) {
                sLastTouchX = x;
                sLastTouchY = y;
            } else {
                float dy = (float)(sLastTouchY - y);
                sAchievementsScrollY += dy;
                if (sAchievementsScrollY < 0.0f) sAchievementsScrollY = 0.0f;
                if (sAchievementsScrollY > maxAchScroll) sAchievementsScrollY = maxAchScroll;
                sLastTouchX = x;
                sLastTouchY = y;
                sIsTouchDragging = true;
            }
        }
        return;
    }

    /* Options Tab — new modal-based layout (no scroll) */
    if (sCurrentTab == BOTTOM_TAB_OPTIONS) {
        /* Confirmation dialog (hardcore enable / restart game) */
        if (sShowConfirmModal) {
            if (isNewTap) {
                if (x >= 40 && x <= 150 && y >= 140 && y <= 168) {
                    /* ACCEPT */
                    bool wasRestart = sConfirmIsRestart;
                    sShowConfirmModal = false;
                    if (!wasRestart) {
                        Port_RA_SetHardcore(true);
                        Port_Config_Save();
                    }
                    TriggerGameRestart();
                } else if (x >= 170 && x <= 280 && y >= 140 && y <= 168) {
                    /* CANCEL */
                    sShowConfirmModal = false;
                }
            }
            return;
        }

        if (sShowRemapModal) {
            /* Remap action selector popup */
            if (sRemapSelectButtonIdx >= 0) {
                if (isNewTap) {
                    /* Check 9 action buttons + cancel (5 in col 0, 4 in col 1 + cancel) */
                    for (int act = 0; act < 9; ++act) {
                        int col = act / 5;
                        int row = act % 5;
                        float bx = (col == 0) ? 18.0f : 162.0f;
                        float by = 48.0f + (float)row * 28.0f;
                        if (x >= (int)bx && x <= (int)(bx + 140.0f) && y >= (int)by && y <= (int)(by + 24.0f)) {
                            Port_Config_SetButtonMapping(sRemapSelectButtonIdx, act);
                            sRemapSelectButtonIdx = -1;
                            return;
                        }
                    }
                    /* Check Cancel button in col 1, row 4 */
                    if (x >= 162 && x <= 302 && y >= (int)(48.0f + 4.0f * 28.0f) && y <= (int)(48.0f + 4.0f * 28.0f + 24.0f)) {
                        sRemapSelectButtonIdx = -1;
                        return;
                    }
                }
                return;
            }

            /* Remap modal main view: scrollable list of 10 buttons + C-Stick mode */
            const float viewY0 = 44.0f;
            const float viewY1 = 202.0f;
            const float maxScroll = 180.0f; /* (13 items * 26px = 338px) - 158px */

            if (isNewTap) {
                /* Check Bottom Buttons (Y: 204 to 230): RESTABLECER (16..156) and CERRAR (164..304) */
                if (y >= 204 && y <= 230) {
                    if (x >= 16 && x <= 156) {
                        /* Reset Defaults */
                        Port_Config_ResetButtonMappingDefault();
                        return;
                    } else if (x >= 164 && x <= 304) {
                        /* Close */
                        sShowRemapModal = false;
                        sRemapScrollY = 0.0f;
                        sRemapSelectButtonIdx = -1;
                        return;
                    }
                }

                /* Direct scrollbar touch on right */
                if (x >= 300 && maxScroll > 0.0f && y >= (int)viewY0 && y <= (int)viewY1) {
                    float trackY = (float)y - viewY0;
                    if (trackY < 0.0f) trackY = 0.0f;
                    if (trackY > (viewY1 - viewY0)) trackY = viewY1 - viewY0;
                    sRemapScrollY = (trackY / (viewY1 - viewY0)) * maxScroll;
                    sIsTouchDragging = true;
                } else if (y >= (int)viewY0 && y <= (int)viewY1 && x >= 10 && x <= 300) {
                    sTouchStartX = x;
                    sTouchStartY = y;
                    sLastTouchX = x;
                    sLastTouchY = y;
                    sIsTouchDragging = false;
                }
            } else if (x >= 300 && maxScroll > 0.0f && y >= (int)viewY0 && y <= (int)viewY1) {
                float trackY = (float)y - viewY0;
                if (trackY < 0.0f) trackY = 0.0f;
                if (trackY > (viewY1 - viewY0)) trackY = viewY1 - viewY0;
                sRemapScrollY = (trackY / (viewY1 - viewY0)) * maxScroll;
                sIsTouchDragging = true;
            } else if (sLastTouchX >= 0 && sLastTouchY >= 0) {
                const float total = (sTouchStartY >= 0) ? (float)(y - sTouchStartY) : 0.0f;
                if (!sIsTouchDragging && total > -TOUCH_DRAG_SLOP && total < TOUCH_DRAG_SLOP) {
                    /* The wobble of a tap: the list stays where it is and the release picks the row. */
                    sLastTouchX = x;
                    sLastTouchY = y;
                } else {
                    float dy = (float)(sLastTouchY - y);
                    sRemapScrollY += dy;
                    if (sRemapScrollY < 0.0f) sRemapScrollY = 0.0f;
                    if (sRemapScrollY > maxScroll) sRemapScrollY = maxScroll;
                    sIsTouchDragging = true;
                    sLastTouchX = x;
                    sLastTouchY = y;
                }
            }
            return;
        }

        if (sShowNotesModal) {
            if (isNewTap) {
                if (x >= 100 && x <= 220 && y >= 204 && y <= 230) {
                    sShowNotesModal = false;
                } else {
                    sLastTouchX = x;
                    sLastTouchY = y;
                }
            } else if (sLastTouchY >= 0) {
                sNotesScrollY += (float)(sLastTouchY - y);
                if (sNotesScrollY < 0.0f) sNotesScrollY = 0.0f;
                if (sNotesScrollY > NotesMaxScroll()) sNotesScrollY = NotesMaxScroll();
                sLastTouchX = x;
                sLastTouchY = y;
            }
            return;
        }

        if (sShowUpdateModal) {
            if (isNewTap) {
                UpdaterState us = Port_Updater_GetState();
                if (x >= 100 && x <= 220 && y >= 204 && y <= 230) {
                    sShowUpdateModal = false;
                } else if (x >= 10 && x <= 308) {
                    if (y >= 56 && y <= 80) {
                        Port_Updater_SetAuto(!Port_Updater_GetAuto());
                        Port_Config_Save();
                    } else if (y >= 84 && y <= 108) {
                        Port_Updater_SetBeta(!Port_Updater_GetBeta());
                        Port_Config_Save();
                    } else if (y >= 176 && y <= 200) {
                        OpenNotesModal();
                    } else if (y >= 140 && y <= 172) {
                        if (us == UPDATER_AVAILABLE) {
                            Port_Updater_Install();
                        } else if (us == UPDATER_INSTALLED) {
                            Port_Updater_Restart();
                        } else if (us != UPDATER_CHECKING && us != UPDATER_DOWNLOADING) {
                            Port_Updater_CheckNow();
                        }
                    }
                }
            }
            return;
        }

        if (sShowDisplayModal) {
            if (isNewTap) {
                if (x >= 100 && x <= 220 && y >= 206 && y <= 230) {
                    sShowDisplayModal = false;
                    return;
                }
                /* Two-column grid: 0..2 cycle a display setting, 3..5 toggle,
                 * 6..8 cycle a GBA effect through OFF/LOW/MED/HIGH. */
                switch (DispCellHit(x, y)) {
                    case 0: Port_Config_CycleLanguage(); break;
                    case 1: Port_Config_Cycle3DSAspectRatio(); break;
                    case 2: Port_Config_Cycle3DSDisplayStyle(); break;
                    case 3: Port_Config_CycleFpsPosition(); break;
                    case 4: Port_Config_SetAutoHideHud(!Port_Config_GetAutoHideHud()); break;
                    case 5: Port_Config_SetHideSpoilers(!Port_Config_GetHideSpoilers()); break;
                    case 6: Port_Config_SetGbaFxGrade((Port_Config_GetGbaFxGrade() + 1) % 4); break;
                    case 7: Port_Config_SetGbaFxGrid((Port_Config_GetGbaFxGrid() + 1) % 4); break;
                    case 8: Port_Config_SetGbaFxVignette((Port_Config_GetGbaFxVignette() + 1) % 4); break;
                    case 9:
                        if (Port_Config_Get3DSDisplayStyle() == 0 && Port_Config_Get3DSAspectRatio() != 3) Port_Config_ToggleHudOutside();
                        break;
                    case 10:
                        if (Port_Config_Get3DSAspectRatio() != 3 &&
                            (Port_Config_Get3DSDisplayStyle() == 0 || Port_Config_Get3DSAspectRatio() == 1)) {
                            Port_Config_ToggleGbaBezel();
                        }
                        break;
                    case 11:
                        Port_Config_SetFramePacing((Port_Config_GetFramePacing() + 1) % 2);
                        break;
                    default: break;
                }
            }
            return;
        }


        /* Main options buttons (no scrolling needed) */
        if (isNewTap) {
            if (x >= 16 && x <= 304 && y >= 48 && y <= 76) {
                sShowDisplayModal = true;
            } else if (x >= 16 && x <= 304 && y >= 82 && y <= 114) {
                sShowRemapModal = true;
                sRemapScrollY = 0.0f;
                sRemapSelectButtonIdx = -1;
            } else if (x >= 16 && x <= 156 && y >= 122 && y <= 150) {
                sConfirmIsRestart = true;
                sShowConfirmModal = true;
            } else if (x >= 164 && x <= 304 && y >= 122 && y <= 150) {
                sShowUpdateModal = true;
            }
        }
    }
}

void Port_BottomUI_TouchReleased(void) {
#ifdef PORT_DEBUG_TOOLS_ACTIVE
    /* Tap-to-warp: a release close to where the touch started counts as a
     * tap, anything further was a pan. */
    if (sDebugMapWarpArmed && sCurrentTab == BOTTOM_TAB_MAP &&
        sTouchStartX >= 0 && sTouchStartY >= 0) {
        /* Distance-only test, and a generous one. Two earlier attempts were
         * both far too strict for a stylus: `!sIsDragging` is useless here
         * because the pan handler sets that flag on a SINGLE pixel of
         * movement, and a 6px radius is smaller than the wobble the act of
         * pressing a stylus against the screen produces on its own -- it
         * took the better part of ten tries to land a warp. 20px is still
         * well under any deliberate pan, which crosses tens of pixels. */
        if (sDebugMapWarpDevSq <= 400) {
            int tx = 0, ty = 0;
            if (MapTileAtTouch(sTouchStartX, sTouchStartY, &tx, &ty)) {
                /* Stays armed until the user manually turns it off */
                int door = PortPpuMzm_DebugRequestWarpToMapTile(sViewArea, tx, ty);
                DebugToolsSetMsg(door >= 0 ? "TELETRANSPORTANDO..." : "SIN PUERTA CERCA");
            }
        }
    }
#endif

    /* Pack chooser: a release that never became a drag opens that set. Row 0
     * is the "all sets" entry, so subsets start at row 1. */
    if (sCurrentTab == BOTTOM_TAB_ACHIEVEMENTS && sShowAchPacksModal) {
        if (!sIsTouchDragging && sTouchStartX >= 16 && sTouchStartX <= 300 &&
            (float)sTouchStartY >= ACH_LIST_CLIP_Y0 && (float)sTouchStartY < ACH_LIST_CLIP_Y1) {
            float local = (float)sTouchStartY - ACH_LIST_CLIP_Y0 + sAchPacksScrollY;
            uint32_t row = (uint32_t)(local / ACH_PACK_CARD_H);
            bool open = false;
            if (row == 0u) {
                Port_RA_SetListSubset(0);
                open = true;
            } else {
                const RetroAchievementSubset* subset = Port_RA_GetSubset(row - 1u);
                if (subset) {
                    Port_RA_SetListSubset(subset->id);
                    open = true;
                }
            }
            if (open) {
                sShowAchPacksModal = false;
                sShowAchievementsModal = true;
                sAchievementsScrollY = 0.0f;
            }
        }
    }

    /* Achievement list: a tap that never became a drag opens the detail popup
     * for that card, same release-based shape as the pack chooser above. */
    if (sCurrentTab == BOTTOM_TAB_ACHIEVEMENTS && sShowAchievementsModal && !sShowAchDetailModal) {
        if (!sIsTouchDragging && sTouchStartX >= 16 && sTouchStartX <= 300 &&
            (float)sTouchStartY >= ACH_LIST_CLIP_Y0 && (float)sTouchStartY < ACH_LIST_CLIP_Y1) {
            float local = (float)sTouchStartY - ACH_LIST_CLIP_Y0 + sAchievementsScrollY;
            uint32_t row = (uint32_t)(local / ACH_CARD_H);
            const RetroAchievementItem* it = Port_RA_GetViewAchievement(row);
            if (it) {
                sAchDetail = *it;
                sShowAchDetailModal = true;
            }
        }
    }

    /* Handle tap selection on remap modal without accidental drag triggering */
    if (sCurrentTab == BOTTOM_TAB_OPTIONS && sShowRemapModal && sRemapSelectButtonIdx < 0) {
        if (!sIsTouchDragging && sTouchStartX >= 0 && sTouchStartY >= 0) {
            float viewY0 = 44.0f;
            float viewY1 = 202.0f;
            if (sTouchStartY >= (int)viewY0 && sTouchStartY <= (int)viewY1 && sTouchStartX >= 16 && sTouchStartX <= 300) {
                float contentY = viewY0 - sRemapScrollY;
                float relY = (float)sTouchStartY - contentY;
                int tapIdx = (int)(relY / 26.0f);
                if (tapIdx >= 0 && tapIdx < 10) {
                    /* Open action picker popup */
                    sRemapSelectButtonIdx = tapIdx;
                } else if (tapIdx == 10) {
                    /* Cycle C-Stick mode directly: 0=OFF, 1=AIM, 2=MOVEMENT, 3=ALL */
                    int mode = Port_Config_GetCstickMode();
                    Port_Config_SetCstickMode((mode + 1) % 4);
                } else if (tapIdx == 11) {
                    /* AUTO BOMB JUMP on / off */
                    extern bool Port_AutoBomb_Enabled(void);
                    extern void Port_AutoBomb_SetEnabled(bool on);
                    Port_AutoBomb_SetEnabled(!Port_AutoBomb_Enabled());
                    Port_Config_Save();
                } else if (tapIdx == 12) {
                    /* AUTO SPACE JUMP on / off */
                    extern bool Port_AutoSpaceJump_Enabled(void);
                    extern void Port_AutoSpaceJump_SetEnabled(bool on);
                    Port_AutoSpaceJump_SetEnabled(!Port_AutoSpaceJump_Enabled());
                    Port_Config_Save();
                }
            }
        }
    }

    if (sCurrentTab == BOTTOM_TAB_STATE) StateTapReleased();

    sLastTouchX = -1;
    sLastTouchY = -1;
    sTouchStartX = -1;
    sTouchStartY = -1;
    sIsDragging = false;
    sIsTouchDragging = false;
    sAchScrollbarDrag = false;
}

/* --- Top navigation bar: icon tabs (left) + system status (right) ------ */

#define TAB_ICON_W    30.0f
#define TAB_ICON_PITCH 34.0f   /* icon width + 4px gap */

/* Shared layout used by both the renderer and the touch handler so a tap
 * always lands on what is drawn. Left-aligned icon tabs; the freed
 * right-hand space carries the clock/battery/wifi widget. */
static int BottomTabLayout(BottomTabSlot slots[BOTTOM_TAB_SLOT_MAX]) {
    bool showDebug = Port_BottomUI_DebugTabVisible();
    int n = 0;
    float x = 4.0f;
    slots[n].x = x; slots[n].w = TAB_ICON_W; slots[n].tab = BOTTOM_TAB_MAP;     slots[n].icon = 0; n++; x += TAB_ICON_PITCH;
    slots[n].x = x; slots[n].w = TAB_ICON_W; slots[n].tab = BOTTOM_TAB_STATUS;  slots[n].icon = 1; n++; x += TAB_ICON_PITCH;
    if (showDebug) {
        slots[n].x = x; slots[n].w = TAB_ICON_W; slots[n].tab = BOTTOM_TAB_DEBUG; slots[n].icon = 2; n++; x += TAB_ICON_PITCH;
    }
    slots[n].x = x; slots[n].w = TAB_ICON_W; slots[n].tab = BOTTOM_TAB_STATE;   slots[n].icon = 4; n++; x += TAB_ICON_PITCH;
    slots[n].x = x; slots[n].w = TAB_ICON_W; slots[n].tab = BOTTOM_TAB_ACHIEVEMENTS; slots[n].icon = 5; n++; x += TAB_ICON_PITCH;
    slots[n].x = x; slots[n].w = TAB_ICON_W; slots[n].tab = BOTTOM_TAB_OPTIONS; slots[n].icon = 3; n++;
    return n;
}

/* One 14x14-ish glyph, all whole pixels, centred at (cx,cy).
 * 0 map (room grid), 1 status (stat bars), 2 debug (bug), 3 options (sliders),
 * 4 save states (floppy), 5 achievements (trophy). */
static void DrawTabIcon(int icon, float cx, float cy, uint32_t col, uint32_t bg) {
    const float x = floorf(cx), y = floorf(cy);
    (void)bg;
    #define R(dx, dy, w, h) C2D_DrawRectSolid(x + (dx), y + (dy), 0.6f, (w), (h), col)
    switch (icon) {
    case 0: /* room grid (floor-plan ring: 3x3 cells, hollow centre) */
        R(-7.0f, -7.0f, 4.0f, 4.0f); R(-2.0f, -7.0f, 4.0f, 4.0f); R(3.0f, -7.0f, 4.0f, 4.0f);
        R(-7.0f, -2.0f, 4.0f, 4.0f);                              R(3.0f, -2.0f, 4.0f, 4.0f);
        R(-7.0f,  3.0f, 4.0f, 4.0f); R(-2.0f,  3.0f, 4.0f, 4.0f); R(3.0f,  3.0f, 4.0f, 4.0f);
        break;
    case 1: /* stat bars -- 3px wide, 1px gaps, evenly spaced */
        R(-6.0f,  1.0f, 3.0f, 4.0f);
        R(-2.0f, -4.0f, 3.0f, 9.0f);
        R( 2.0f, -1.0f, 3.0f, 6.0f);
        break;
    case 2: /* bug: head + antennae, body with a dorsal line, three legs a side
             * (whole glyph sits 1px low so it reads centred in the tab) */
        R(-1.0f, -7.0f, 1.0f, 2.0f); R( 1.0f, -7.0f, 1.0f, 2.0f);
        R(-2.0f, -5.0f, 4.0f, 2.0f);
        R(-3.0f, -3.0f, 3.0f, 8.0f); R( 1.0f, -3.0f, 3.0f, 8.0f);
        R(-2.0f, -4.0f, 4.0f, 1.0f); R(-2.0f,  5.0f, 4.0f, 1.0f);
        R(-6.0f, -2.0f, 3.0f, 1.0f); R(-6.0f, 1.0f, 3.0f, 1.0f); R(-6.0f, 4.0f, 3.0f, 1.0f);
        R( 3.0f, -2.0f, 3.0f, 1.0f); R( 3.0f, 1.0f, 3.0f, 1.0f); R( 3.0f, 4.0f, 3.0f, 1.0f);
        break;
    case 4: /* save state: floppy disk */
        R(-6.0f, -6.0f, 12.0f, 1.0f);   /* body: top edge */
        R(-6.0f,  5.0f, 12.0f, 1.0f);   /* body: bottom edge */
        R(-6.0f, -6.0f, 1.0f, 12.0f);   /* body: left edge */
        R( 5.0f, -5.0f, 1.0f, 11.0f);   /* body: right edge (clipped corner) */
        R( 3.0f, -6.0f, 3.0f, 3.0f);    /* clipped top-right corner */
        R(-3.0f, -6.0f, 5.0f, 4.0f);    /* metal shutter */
        R(-4.0f,  1.0f, 8.0f, 4.0f);    /* label area */
        break;
    case 5: /* achievements: trophy cup, handles, stem, base */
        R(-4.0f, -6.0f, 8.0f, 5.0f);
        R(-3.0f, -1.0f, 6.0f, 2.0f);
        R(-6.0f, -5.0f, 2.0f, 3.0f);
        R( 4.0f, -5.0f, 2.0f, 3.0f);
        R(-1.0f,  1.0f, 2.0f, 3.0f);
        R(-4.0f,  4.0f, 8.0f, 2.0f);
        break;
    default: /* sliders (nudged 1px low to read centred in the tab) */
        R(-6.0f, -4.0f, 12.0f, 1.0f);
        R(-6.0f,  0.0f, 12.0f, 1.0f);
        R(-6.0f,  4.0f, 12.0f, 1.0f);
        R(-2.0f, -5.0f, 3.0f, 3.0f);
        R( 2.0f, -1.0f, 3.0f, 3.0f);
        R(-5.0f,  3.0f, 3.0f, 3.0f);
        break;
    }
    #undef R
}

/* Clock (HH:MM), wifi bars and a battery gauge, right-aligned in the bar. */
static void DrawSystemStatus(void) {
    const uint32_t frameCol = C2D_Color32(120, 132, 158, 255);
    const uint32_t dimCol   = C2D_Color32(58, 66, 84, 255);
    const uint32_t textCol  = C2D_Color32(210, 220, 235, 255);
    const float cy = 13.0f;

    /* Battery, at the far right. 15x8 shell + a 2x4 nub. */
    float bx = 299.0f, by = cy - 4.0f;
    C2D_DrawRectSolid(bx, by, 0.6f, 15.0f, 1.0f, frameCol);
    C2D_DrawRectSolid(bx, by + 7.0f, 0.6f, 15.0f, 1.0f, frameCol);
    C2D_DrawRectSolid(bx, by, 0.6f, 1.0f, 8.0f, frameCol);
    C2D_DrawRectSolid(bx + 14.0f, by, 0.6f, 1.0f, 8.0f, frameCol);
    C2D_DrawRectSolid(bx + 15.0f, by + 2.0f, 0.6f, 2.0f, 4.0f, frameCol);
    if (sPtmuReady) {
        int lvl = sBattLevel; if (lvl > 5) lvl = 5;
        /* Interior is cols bx+1..bx+13 / rows by+1..by+6; inset the fill 1px on
         * every side (max width 11, so a full gauge still shows a right margin). */
        float fillW = (float)lvl * (11.0f / 5.0f);
        uint32_t fc = sBattCharging      ? C2D_Color32(90, 210, 120, 255)
                    : lvl <= 1           ? C2D_Color32(230, 80, 60, 255)
                    : lvl == 2           ? C2D_Color32(235, 190, 70, 255)
                                         : C2D_Color32(110, 205, 130, 255);
        if (fillW > 0.0f) C2D_DrawRectSolid(bx + 2.0f, by + 2.0f, 0.61f, fillW, 4.0f, fc);
        if (sBattCharging) {
            /* small bolt, dark so it reads on the light-green charging fill */
            const uint32_t boltCol = C2D_Color32(15, 60, 30, 255);
            C2D_DrawTriangle(bx + 8.0f, by + 1.0f, boltCol, bx + 5.0f, by + 5.0f, boltCol,
                             bx + 8.0f, by + 5.0f, boltCol, 0.63f);
            C2D_DrawTriangle(bx + 7.0f, by + 3.0f, boltCol, bx + 10.0f, by + 3.0f, boltCol,
                             bx + 7.0f, by + 6.0f, boltCol, 0.63f);
        }
    } else {
        C2D_DrawRectSolid(bx + 5.0f, by + 3.0f, 0.61f, 4.0f, 2.0f, dimCol);
    }

    /* Wifi: three rising bars, lit up to the current strength (0..3). */
    int wifi = (int)osGetWifiStrength();      /* 0..3 */
    float wx = bx - 16.0f;
    for (int i = 0; i < 3; ++i) {
        float h = 3.0f + (float)i * 2.0f;
        /* Bottom-aligned to the battery shell's lower edge (by + 8 == cy + 4)
         * so the whole cluster shares one baseline. */
        C2D_DrawRectSolid(wx + (float)i * 4.0f, cy + 4.0f - h, 0.6f, 2.0f, h,
                          (i < wifi) ? frameCol : dimCol);
    }

    /* Clock, right edge just left of the wifi bars. */
    time_t t = time(NULL);
    struct tm lt;
    char buf[8] = "--:--";
    if (t != (time_t)-1 && localtime_r(&t, &lt))
        snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    DrawText(wx - 4.0f - 5.0f * 6.0f, cy - 3.0f, 1.0f, buf, textCol);
}

/* Render Navigation Bar (icon tabs + system status) */
static void RenderTabBar(void) {
    BottomTabSlot slots[BOTTOM_TAB_SLOT_MAX];
    int tabCount = BottomTabLayout(slots);

    /* Health tint: only active during real gameplay. Blinks to catch attention
     * even without looking directly at the screen. */
    bool inRealGameplay = ((uint8_t)gMainGameMode == 4 /* GM_INGAME */);
    uint16_t curE = gEquipment.currentEnergy;
    bool warnYellow = inRealGameplay && curE >= 30 && curE < 60;
    bool warnRed    = inRealGameplay && curE < 30;
    bool blinkOn    = ((sFrameCounter & 0x0F) < 8);

    for (int i = 0; i < tabCount; ++i) {
        bool active = (sCurrentTab == slots[i].tab);
        uint32_t bg, border, iconColor;

        if (warnRed) {
            if (blinkOn) {
                bg = active ? C2D_Color32(140, 28, 22, 255) : C2D_Color32(60, 18, 16, 255);
                border = active ? C2D_Color32(230, 55, 40, 255) : C2D_Color32(100, 30, 24, 255);
            } else {
                bg = active ? C2D_Color32(80, 18, 18, 255) : C2D_Color32(35, 12, 12, 255);
                border = active ? C2D_Color32(160, 38, 30, 255) : C2D_Color32(65, 22, 18, 255);
            }
            iconColor = active ? C2D_Color32(255, 200, 190, 255) : C2D_Color32(200, 120, 110, 255);
        } else if (warnYellow) {
            if (blinkOn) {
                bg = active ? C2D_Color32(130, 100, 24, 255) : C2D_Color32(55, 44, 16, 255);
                border = active ? C2D_Color32(230, 180, 45, 255) : C2D_Color32(100, 80, 28, 255);
            } else {
                bg = active ? C2D_Color32(90, 70, 18, 255) : C2D_Color32(38, 32, 12, 255);
                border = active ? C2D_Color32(170, 130, 35, 255) : C2D_Color32(70, 58, 22, 255);
            }
            iconColor = active ? C2D_Color32(255, 245, 200, 255) : C2D_Color32(200, 175, 110, 255);
        } else {
            bg = active ? C2D_Color32(18, 70, 130, 255) : C2D_Color32(26, 30, 42, 255);
            border = active ? C2D_Color32(45, 150, 240, 255) : C2D_Color32(50, 56, 75, 255);
            iconColor = active ? C2D_Color32(255, 255, 255, 255) : C2D_Color32(140, 150, 175, 255);
        }

        C2D_DrawRectSolid(slots[i].x, 3.0f, 0.4f, slots[i].w, 20.0f, border);
        C2D_DrawRectSolid(slots[i].x + 1.0f, 4.0f, 0.5f, slots[i].w - 2.0f, 18.0f, bg);
        DrawTabIcon(slots[i].icon, slots[i].x + slots[i].w / 2.0f, 13.0f, iconColor, bg);
    }

    DrawSystemStatus();
}

/* Decodes and draws an authentic 8x8 4bpp GBA minimap tile with correct ROM palette */
static void DrawAuthenticMapTile(float dstX, float dstY, float tileSize, uint16_t tileData,
                                float clipX0, float clipY0, float clipX1, float clipY1, float zDepth) {
    if (tileData == 0 || tileData == 0x140) return;

    uint32_t rawTileId = tileData & 0x3FF;
    bool xFlip = (tileData & 0x400) != 0;
    bool yFlip = (tileData & 0x800) != 0;
    uint32_t palId = (tileData >> 12) & 0xF;
    if (palId >= 5) palId = 0;

    const uint8_t* tileGfx = NULL;

    /* p_sMinimapTilesGfx points to the continuous tile graphic ROM array */
    if (rawTileId < 400 && p_sMinimapTilesGfx) {
        tileGfx = &((const uint8_t*)*p_sMinimapTilesGfx)[rawTileId * 32];
    } else if (rawTileId >= 0x200 && rawTileId < (0x200 + 400) && p_sMinimapTilesGfx) {
        tileGfx = &((const uint8_t*)*p_sMinimapTilesGfx)[(rawTileId - 0x200) * 32];
    }

    /* Fallback if tile graphic could not be found */
    if (!tileGfx || !p_sMinimapTilesPal) {
        float rx0 = dstX < clipX0 ? clipX0 : dstX;
        float ry0 = dstY < clipY0 ? clipY0 : dstY;
        float rx1 = (dstX + tileSize) > clipX1 ? clipX1 : (dstX + tileSize);
        float ry1 = (dstY + tileSize) > clipY1 ? clipY1 : (dstY + tileSize);
        if (rx1 > rx0 && ry1 > ry0) {
            uint32_t fbCol = (palId == 1) ? C2D_Color32(30, 140, 60, 255) : C2D_Color32(20, 50, 110, 255);
            C2D_DrawRectSolid(rx0, ry0, zDepth, rx1 - rx0, ry1 - ry0, fbCol);
        }
        return;
    }

    const uint16_t* palette = &(*p_sMinimapTilesPal)[palId * 16];
    float pixelSize = tileSize / 8.0f;

    for (int row = 0; row < 8; ++row) {
        int r = yFlip ? (7 - row) : row;
        const uint8_t* rowBytes = &tileGfx[r * 4];
        float py0 = dstY + (float)row * pixelSize;
        float py1 = py0 + pixelSize;
        if (py1 <= clipY0 || py0 >= clipY1) continue;

        for (int col = 0; col < 8;) {
            int c = xFlip ? (7 - col) : col;
            uint8_t byteVal = rowBytes[c / 2];
            uint8_t colorIdx = (c & 1) ? (byteVal >> 4) : (byteVal & 0xF);

            if (colorIdx == 0) {
                col++;
                continue;
            }

            int startCol = col;
            col++;
            while (col < 8) {
                int nextC = xFlip ? (7 - col) : col;
                uint8_t nextByte = rowBytes[nextC / 2];
                uint8_t nextColor = (nextC & 1) ? (nextByte >> 4) : (nextByte & 0xF);
                if (nextColor != colorIdx) break;
                col++;
            }

            float px0 = dstX + (float)startCol * pixelSize;
            float px1 = dstX + (float)col * pixelSize;
            if (px1 <= clipX0 || px0 >= clipX1) continue;

            float drawX0 = px0 < clipX0 ? clipX0 : px0;
            float drawX1 = px1 > clipX1 ? clipX1 : px1;
            float drawY0 = py0 < clipY0 ? clipY0 : py0;
            float drawY1 = py1 > clipY1 ? clipY1 : py1;

            uint32_t color = Bgr555ToRgba8(palette[colorIdx], false);
            C2D_DrawRectSolid(drawX0, drawY0, zDepth, drawX1 - drawX0, drawY1 - drawY0, color);
        }
    }
}

/* Authentic HUD Item Sprites */
static void DrawEnergyIcon(float x, float y) {
    C2D_DrawRectSolid(x, y, 0.6f, 14.0f, 10.0f, C2D_Color32(30, 45, 70, 255));
    C2D_DrawRectSolid(x + 1.0f, y + 1.0f, 0.65f, 12.0f, 8.0f, C2D_Color32(10, 20, 35, 255));
    C2D_DrawRectSolid(x, y + 2.0f, 0.7f, 2.0f, 6.0f, C2D_Color32(180, 195, 220, 255));
    C2D_DrawRectSolid(x + 12.0f, y + 2.0f, 0.7f, 2.0f, 6.0f, C2D_Color32(180, 195, 220, 255));
    C2D_DrawRectSolid(x + 4.0f, y + 2.0f, 0.75f, 6.0f, 2.0f, C2D_Color32(255, 60, 140, 255));
    C2D_DrawRectSolid(x + 4.0f, y + 4.0f, 0.75f, 4.0f, 2.0f, C2D_Color32(255, 120, 180, 255));
    C2D_DrawRectSolid(x + 4.0f, y + 6.0f, 0.75f, 6.0f, 2.0f, C2D_Color32(255, 60, 140, 255));
    C2D_DrawRectSolid(x + 3.0f, y + 2.0f, 0.75f, 2.0f, 6.0f, C2D_Color32(255, 200, 220, 255));
}

static void DrawMissileIcon(float x, float y) {
    C2D_DrawRectSolid(x + 1.0f, y + 3.0f, 0.6f, 3.0f, 4.0f, C2D_Color32(220, 230, 240, 255));
    C2D_DrawRectSolid(x + 3.0f, y + 2.0f, 0.65f, 2.0f, 6.0f, C2D_Color32(160, 180, 200, 255));
    C2D_DrawRectSolid(x + 5.0f, y + 2.0f, 0.6f, 7.0f, 6.0f, C2D_Color32(230, 35, 35, 255));
    C2D_DrawRectSolid(x + 6.0f, y + 3.0f, 0.65f, 5.0f, 2.0f, C2D_Color32(255, 120, 120, 255));
    C2D_DrawRectSolid(x + 10.0f, y, 0.6f, 3.0f, 2.0f, C2D_Color32(180, 195, 215, 255));
    C2D_DrawRectSolid(x + 10.0f, y + 8.0f, 0.6f, 3.0f, 2.0f, C2D_Color32(180, 195, 215, 255));
}

static void DrawSuperMissileIcon(float x, float y) {
    C2D_DrawRectSolid(x + 1.0f, y + 3.0f, 0.6f, 3.0f, 4.0f, C2D_Color32(240, 255, 220, 255));
    C2D_DrawRectSolid(x + 3.0f, y + 2.0f, 0.65f, 2.0f, 6.0f, C2D_Color32(180, 220, 160, 255));
    C2D_DrawRectSolid(x + 5.0f, y + 2.0f, 0.6f, 7.0f, 6.0f, C2D_Color32(30, 210, 60, 255));
    C2D_DrawRectSolid(x + 6.0f, y + 3.0f, 0.65f, 5.0f, 2.0f, C2D_Color32(140, 255, 160, 255));
    C2D_DrawRectSolid(x + 10.0f, y, 0.6f, 3.0f, 2.0f, C2D_Color32(20, 140, 45, 255));
    C2D_DrawRectSolid(x + 10.0f, y + 8.0f, 0.6f, 3.0f, 2.0f, C2D_Color32(20, 140, 45, 255));
}

static void DrawPowerBombIcon(float x, float y) {
    C2D_DrawRectSolid(x + 2.0f, y, 0.6f, 8.0f, 10.0f, C2D_Color32(255, 180, 0, 255));
    C2D_DrawRectSolid(x, y + 2.0f, 0.6f, 12.0f, 6.0f, C2D_Color32(255, 180, 0, 255));
    C2D_DrawRectSolid(x + 2.0f, y + 2.0f, 0.65f, 8.0f, 6.0f, C2D_Color32(255, 240, 80, 255));
    C2D_DrawRectSolid(x + 4.0f, y + 3.0f, 0.7f, 4.0f, 4.0f, C2D_Color32(255, 100, 0, 255));
    C2D_DrawRectSolid(x + 5.0f, y + 4.0f, 0.75f, 2.0f, 2.0f, C2D_Color32(255, 255, 255, 255));
}

/* Thin remaining-ammo/health gauge: a sunk dark track with a 1px top edge
 * and a tinted fill for cur/max. 3px tall so it tucks under the number
 * inside the status header card. */
static void DrawResourceBar(float x, float y, float w, uint32_t fillCol, unsigned cur, unsigned max) {
    C2D_DrawRectSolid(x, y, 0.47f, w, 3.0f, C2D_Color32(8, 12, 20, 255));
    C2D_DrawRectSolid(x, y, 0.475f, w, 1.0f, C2D_Color32(46, 56, 78, 255));
    if (max > 0 && cur > 0) {
        float fw = w * (float)cur / (float)max;
        if (fw < 1.0f) fw = 1.0f;
        if (fw > w) fw = w;
        C2D_DrawRectSolid(x, y, 0.48f, fw, 3.0f, fillCol);
    }
}

static const char* Port_Config_GetLanguageDisplayName(int lang) {
    switch (lang) {
        case 0: return "0: JAPANESE (KANJI)";
        case 1: return "1: JAPANESE (HIRAGANA)";
        case 2: return "2: ENGLISH";
        case 3: return "3: DEUTSCH";
        case 4: return "4: FRANÇAIS";
        case 5: return "5: ITALIANO";
        case 6: return "6: ESPAÑOL";
        default: return "2: ENGLISH";
    }
}

static void Port_Config_CycleLanguage(void) {
    extern void SramWrite_Language(void);
    gLanguage = (gLanguage + 1) % LANGUAGE_COUNT;
    SramWrite_Language();
    Port_Config_Save();
}

static const char* GetAspectRatioDisplayName(int lang) {
    /* Same order as the language ids used across this file: 0/1/2 EN, 3 DE,
     * 4 FR, 5 IT, 6 ES. */
    static const char* const originalL[7] = { "ORIGINAL (3:2)", "ORIGINAL (3:2)", "ORIGINAL (3:2)", "ORIGINAL (3:2)",
                                              "ORIGINAL (3:2)", "ORIGINALE (3:2)", "ORIGINAL (3:2)" };
    static const char* const originalPixelL[7] = { "ORIGINAL (1:1)", "ORIGINAL (1:1)", "ORIGINAL (1:1)", "ORIGINAL (1:1)",
                                                   "ORIGINAL (1:1)", "ORIGINALE (1:1)", "ORIGINAL (1:1)" };
    static const char* const stretchL[7] = { "STRETCH (16:9)", "STRETCH (16:9)", "STRETCH (16:9)", "GESTRECKT (16:9)",
                                             "ETIRE (16:9)", "ALLARGATO (16:9)", "ESTIRADO (16:9)" };
    /* WIDE shows the world the GBA hides past its frame instead of stretching. */
    static const char* const wideL[7] = { "WIDE", "WIDE", "WIDE", "WEIT", "LARGE", "AMPIO", "AMPLIO" };

    if (lang < 0 || lang > 6) lang = 0;
    int ar = Port_Config_Get3DSAspectRatio();
    if (ar == 3) return wideL[lang];
    /* STRETCH has no meaning at 1:1, so PIXEL PERFECT only offers ORIGINAL / WIDE. */
    if (Port_Config_Get3DSDisplayStyle() == 0) return originalPixelL[lang];
    return ar == 2 ? stretchL[lang] : originalL[lang];
}

static const char* GetDisplayStyleDisplayName(int lang) {
    int ds = Port_Config_Get3DSDisplayStyle();
    switch (lang) {
        case 0:
        case 1:
            switch (ds) {
                case 0: return "PIXEL PERFECT (1:1)";
                case 1: return "SCALED (SHARP)";
                case 2: return "BLUR (SMOOTH)";
                default: return "SCALED";
            }
        case 3: /* DE */
            switch (ds) {
                case 0: return "PIXELGENAU (1:1)";
                case 1: return "SCHARF";
                case 2: return "GEGLAETTET";
                default: return "SKALIERT";
            }
        case 4: /* FR */
            switch (ds) {
                case 0: return "PIXEL PARFAIT (1:1)";
                case 1: return "NET (SHARP)";
                case 2: return "LISSE (SMOOTH)";
                default: return "ECHELLE";
            }
        case 5: /* IT */
            switch (ds) {
                case 0: return "PIXEL PERFECT (1:1)";
                case 1: return "NITIDO (SHARP)";
                case 2: return "SMUSSATO (SMOOTH)";
                default: return "SCALATO";
            }
        case 6: /* ES */
            switch (ds) {
                case 0: return "PIXEL PERFECT (1:1)";
                case 1: return "ESCALADO NITIDO";
                case 2: return "SUAVIZADO";
                default: return "ESCALADO";
            }
        default: /* EN */
            switch (ds) {
                case 0: return "PIXEL PERFECT (1:1)";
                case 1: return "SCALED (SHARP)";
                case 2: return "BLUR (SMOOTH)";
                default: return "SCALED";
            }
    }
}

static const char* GetFpsOverlayDisplayName(int lang) {
    int pos = Port_Config_GetFpsPosition();
    switch (pos) {
        case 1: /* BOTTOM-LEFT */
            switch (lang) {
                case 3: return "UNTEN LINKS";
                case 4: return "BAS GAUCHE";
                case 5: return "BASSO SINISTRA";
                case 6: return "INF. IZQ.";
                default: return "BOTTOM-LEFT";
            }
        case 2: /* BOTTOM-RIGHT */
            switch (lang) {
                case 3: return "UNTEN RECHTS";
                case 4: return "BAS DROITE";
                case 5: return "BASSO DESTRA";
                case 6: return "INF. DER.";
                default: return "BOTTOM-RIGHT";
            }
        case 3: /* TOP-LEFT */
            switch (lang) {
                case 3: return "OBEN LINKS";
                case 4: return "HAUT GAUCHE";
                case 5: return "ALTO SINISTRA";
                case 6: return "SUP. IZQ.";
                default: return "TOP-LEFT";
            }
        case 4: /* TOP-RIGHT */
            switch (lang) {
                case 3: return "OBEN RECHTS";
                case 4: return "HAUT DROITE";
                case 5: return "ALTO DESTRA";
                case 6: return "SUP. DER.";
                default: return "TOP-RIGHT";
            }
        case 0:
        default:
            switch (lang) {
                case 3: return "AUS";
                case 4: return "DESACTIVE";
                case 5: return "DISATTIVO";
                case 6: return "DESACTIVADO";
                default: return "OFF";
            }
    }
}

struct AreaItemStats {
    uint8_t energyObtained;
    uint8_t energyTotal;
    uint8_t missileObtained;
    uint8_t missileTotal;
    uint8_t superObtained;
    uint8_t superTotal;
    uint8_t powerBombObtained;
    uint8_t powerBombTotal;
    uint8_t totalObtained;
    uint8_t totalItems;
};

static const uint8_t sTotalTanksTable[7][4] = {
    { 3, 10, 1, 0 }, /* Brinstar */
    { 2,  9, 0, 0 }, /* Kraid */
    { 1, 13, 2, 1 }, /* Norfair */
    { 3, 13, 3, 0 }, /* Ridley */
    { 0,  1, 0, 1 }, /* Tourian */
    { 0,  3, 1, 1 }, /* Crateria */
    { 3,  1, 8, 6 }, /* Chozodia */
};

extern void Port_GetAreaItemTypeCounts(int area, int* outEnergy, int* outMissile, int* outSuper, int* outPowerBomb);

static void GetAreaStats(int area, struct AreaItemStats* outStats) {
    if (!outStats) return;
    memset(outStats, 0, sizeof(struct AreaItemStats));
    if (area < 0 || area >= 7) return;

    outStats->energyTotal = sTotalTanksTable[area][0];
    outStats->missileTotal = sTotalTanksTable[area][1];
    outStats->superTotal = sTotalTanksTable[area][2];
    outStats->powerBombTotal = sTotalTanksTable[area][3];
    outStats->totalItems = outStats->energyTotal + outStats->missileTotal + outStats->superTotal + outStats->powerBombTotal;

    int energyGot = 0, missileGot = 0, superGot = 0, powerBombGot = 0;
    Port_GetAreaItemTypeCounts(area, &energyGot, &missileGot, &superGot, &powerBombGot);
    outStats->energyObtained = (uint8_t)energyGot;
    outStats->missileObtained = (uint8_t)missileGot;
    outStats->superObtained = (uint8_t)superGot;
    outStats->powerBombObtained = (uint8_t)powerBombGot;

    /* Total = sum of the four tank/ammo types, so it always matches the
     * per-type breakdown above (the map's obtained-items bitmap also logs
     * major items, which are not part of these totals). */
    outStats->totalObtained = (uint8_t)(energyGot + missileGot + superGot + powerBombGot);
}

static void GetGlobalItemStats(struct AreaItemStats* outStats) {
    if (!outStats) return;
    memset(outStats, 0, sizeof(struct AreaItemStats));

    outStats->energyTotal = 12;
    outStats->missileTotal = 50;
    outStats->superTotal = 15;
    outStats->powerBombTotal = 9;
    outStats->totalItems = 86;

    for (int a = 0; a < 7; ++a) {
        struct AreaItemStats aStats;
        GetAreaStats(a, &aStats);
        outStats->totalObtained += aStats.totalObtained;
        outStats->energyObtained += aStats.energyObtained;
        outStats->missileObtained += aStats.missileObtained;
        outStats->superObtained += aStats.superObtained;
        outStats->powerBombObtained += aStats.powerBombObtained;
    }
}

/* Render Collectibles Per-Area Breakdown Modal */
static void RenderCollectiblesModal(int lang) {
    C2D_DrawRectSolid(10.0f, 26.0f, 0.85f, 300.0f, 206.0f, C2D_Color32(10, 14, 24, 252));
    C2D_DrawRectSolid(10.0f, 26.0f, 0.84f, 300.0f, 206.0f, C2D_Color32(40, 70, 120, 255));

    /* Hardcore mode or Hide Spoilers: only show what has already been collected -- never totals
     * or remaining counts (that would leak achievement-relevant info / spoilers). */
    bool hideSpoilers = Port_RA_IsHardcore() || Port_Config_GetHideSpoilers();

    struct AreaItemStats globalStats;
    GetGlobalItemStats(&globalStats);
    char globTitle[64];
    static const char* const collTitles[7] = {
        "COLLECTIBLES BY AREA", "COLLECTIBLES BY AREA", "COLLECTIBLES BY AREA",
        "OBJEKTE NACH GEBIET", "OBJETS PAR REGION", "OGGETTI PER AREA", "COLECCIONABLES POR ZONA"
    };
    if (hideSpoilers) {
        snprintf(globTitle, sizeof(globTitle), "%s: %u", collTitles[lang],
                 globalStats.totalObtained);
    } else {
        unsigned pct = globalStats.totalItems > 0 ? (globalStats.totalObtained * 100 / globalStats.totalItems) : 0;
        snprintf(globTitle, sizeof(globTitle), "%s: %u/%u (%u%%)", collTitles[lang],
                 globalStats.totalObtained, globalStats.totalItems, pct);
    }
    DrawText(18.0f, 32.0f, 1.0f, globTitle, C2D_Color32(255, 215, 0, 255));

    /* Table Headers */
    static const char* const areaHeaderTitles[7] = {
        "AREA", "AREA", "AREA",
        "GEBIET", "REGION", "AREA", "ZONA"
    };
    DrawText(18.0f, 48.0f, 1.0f, areaHeaderTitles[lang], C2D_Color32(100, 220, 255, 255));
    DrawEnergyIcon(108.0f, 46.0f);
    DrawMissileIcon(148.0f, 46.0f);
    DrawSuperMissileIcon(193.0f, 46.0f);
    DrawPowerBombIcon(233.0f, 46.0f);
    if (!hideSpoilers) DrawText(272.0f, 48.0f, 1.0f, "TOT", C2D_Color32(255, 255, 255, 255));

    for (int a = 0; a < 7; ++a) {
        struct AreaItemStats aStats;
        GetAreaStats(a, &aStats);
        float py = 64.0f + (float)a * 19.0f;

        bool hl = (a == sCollectiblesHighlightArea);
        C2D_DrawRectSolid(16.0f, py, 0.88f, 288.0f, 17.0f,
                          hl ? C2D_Color32(28, 54, 44, 255)
                             : (a % 2 == 0) ? C2D_Color32(20, 26, 40, 255) : C2D_Color32(14, 18, 30, 255));
        if (hl) {
            C2D_DrawRectSolid(16.0f, py, 0.885f, 288.0f, 1.0f, C2D_Color32(90, 220, 150, 255));
            C2D_DrawRectSolid(16.0f, py + 16.0f, 0.885f, 288.0f, 1.0f, C2D_Color32(90, 220, 150, 255));
            C2D_DrawRectSolid(16.0f, py, 0.885f, 1.0f, 17.0f, C2D_Color32(90, 220, 150, 255));
            C2D_DrawRectSolid(303.0f, py, 0.885f, 1.0f, 17.0f, C2D_Color32(90, 220, 150, 255));
        }

        DrawText(20.0f, py + 5.0f, 1.0f, AreaName(a),
                 hl ? C2D_Color32(180, 255, 210, 255) : C2D_Color32(220, 235, 255, 255));

        char cBuf[16];
        if (hideSpoilers) {
            snprintf(cBuf, sizeof(cBuf), "%u", aStats.energyObtained);
            DrawText(115.0f, py + 5.0f, 1.0f, cBuf, aStats.energyObtained > 0 ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(90, 100, 120, 255));

            snprintf(cBuf, sizeof(cBuf), "%u", aStats.missileObtained);
            DrawText(155.0f, py + 5.0f, 1.0f, cBuf, aStats.missileObtained > 0 ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(90, 100, 120, 255));

            snprintf(cBuf, sizeof(cBuf), "%u", aStats.superObtained);
            DrawText(200.0f, py + 5.0f, 1.0f, cBuf, aStats.superObtained > 0 ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(90, 100, 120, 255));

            snprintf(cBuf, sizeof(cBuf), "%u", aStats.powerBombObtained);
            DrawText(240.0f, py + 5.0f, 1.0f, cBuf, aStats.powerBombObtained > 0 ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(90, 100, 120, 255));
        } else {
            snprintf(cBuf, sizeof(cBuf), "%u/%u", aStats.energyObtained, aStats.energyTotal);
            DrawText(105.0f, py + 5.0f, 1.0f, cBuf, aStats.energyObtained == aStats.energyTotal ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(255, 215, 0, 255));

            snprintf(cBuf, sizeof(cBuf), "%u/%u", aStats.missileObtained, aStats.missileTotal);
            DrawText(145.0f, py + 5.0f, 1.0f, cBuf, aStats.missileObtained == aStats.missileTotal ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(255, 140, 140, 255));

            snprintf(cBuf, sizeof(cBuf), "%u/%u", aStats.superObtained, aStats.superTotal);
            DrawText(190.0f, py + 5.0f, 1.0f, cBuf, aStats.superObtained == aStats.superTotal ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(100, 255, 140, 255));

            snprintf(cBuf, sizeof(cBuf), "%u/%u", aStats.powerBombObtained, aStats.powerBombTotal);
            DrawText(230.0f, py + 5.0f, 1.0f, cBuf, aStats.powerBombObtained == aStats.powerBombTotal ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(255, 225, 80, 255));

            snprintf(cBuf, sizeof(cBuf), "%u/%u", aStats.totalObtained, aStats.totalItems);
            DrawText(268.0f, py + 5.0f, 1.0f, cBuf, aStats.totalObtained == aStats.totalItems ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(255, 255, 255, 255));
        }
    }

    static const char* const closeCollectiblesLabels[7] = {
        "CLOSE", "CLOSE", "CLOSE",
        "SCHLIESSEN", "FERMER", "CHIUDI", "CERRAR"
    };
    DrawButton(116.0f, 204.0f, 88.0f, 20.0f, closeCollectiblesLabels[lang],
               C2D_Color32(255, 255, 255, 255), BTN_BLUE_BODY, BTN_BLUE_BORDER);
}

static const char* AchSortLabel(int lang) {
    /* [sort mode][lang]; langs 0-2 English, 3 DE, 4 FR, 5 IT, 6 ES. */
    static const char* labels[RA_SORT_COUNT][7] = {
        { "DEFAULT", "DEFAULT", "DEFAULT", "STANDARD", "DEFAUT", "PREDEFINITO", "POR DEFECTO" },
        { "A-Z", "A-Z", "A-Z", "A-Z", "A-Z", "A-Z", "A-Z" },
        { "POINTS", "POINTS", "POINTS", "PUNKTE", "POINTS", "PUNTI", "PUNTOS" },
        { "RECENT", "RECENT", "RECENT", "NEUESTE", "RECENTS", "RECENTI", "RECIENTES" },
    };
    RetroAchievementSort sort = Port_RA_GetListSort();
    if (sort >= RA_SORT_COUNT) sort = RA_SORT_DEFAULT;
    return labels[sort][lang];
}

/* Footer button, bottom-left, shared by the list and the set chooser: that
 * strip is free in both, so the settings need no room from the list itself.
 * ACH_SETTINGS_BTN_* (above) is also the touch hit box. */
static void DrawAchSettingsButton(int lang) {
    static const char* const labels[7] = {
        "SETTINGS", "SETTINGS", "SETTINGS",
        "OPTIONEN", "REGLAGES", "OPZIONI", "AJUSTES"
    };
    DrawButton((float)ACH_SETTINGS_BTN_X, 204.0f, (float)ACH_SETTINGS_BTN_W, 20.0f, labels[lang],
               C2D_Color32(120, 255, 175, 255), C2D_Color32(16, 50, 32, 255), C2D_Color32(55, 150, 95, 255));
}

/* Pack chooser: one card per set plus an "all sets" entry at the top. */
static void RenderAchPacksModal(int lang) {
    const float mX = 10.0f;
    const float mY = 26.0f;
    const float mW = 300.0f;
    const float mH = 206.0f;
    const float clipY0 = ACH_LIST_CLIP_Y0;
    const float clipY1 = ACH_LIST_CLIP_Y1;

    C2D_DrawRectSolid(mX, mY, 0.85f, mW, mH, C2D_Color32(10, 14, 24, 250));
    C2D_DrawRectSolid(mX, mY, 0.84f, mW, mH, C2D_Color32(40, 70, 120, 255));

    static const char* titles[7] = {
        "ACHIEVEMENT SETS", "ACHIEVEMENT SETS", "ACHIEVEMENT SETS",
        "ERFOLGSSAETZE", "SERIES DE SUCCES", "SERIE DI OBIETTIVI", "SERIES DE LOGROS"
    };
    DrawText(20.0f, 32.0f, 1.0f, titles[lang], C2D_Color32(255, 215, 0, 255));

    static const char* allLabels[7] = {
        "ALL SETS", "ALL SETS", "ALL SETS",
        "ALLE SAETZE", "TOUTES LES SERIES", "TUTTE LE SERIE", "TODAS LAS SERIES"
    };

    float maxScroll = AchPacksMaxScroll();
    if (sAchPacksScrollY > maxScroll) sAchPacksScrollY = maxScroll;

    uint32_t subsetCount = Port_RA_GetSubsetCount();
    for (uint32_t row = 0; row <= subsetCount; ++row) {
        float py = clipY0 - sAchPacksScrollY + (float)row * ACH_PACK_CARD_H;
        if (py + ACH_PACK_CARD_H <= clipY0 || py >= clipY1) continue;

        const char* name;
        uint32_t total, unlocked, hardcore;
        if (row == 0) {
            name = allLabels[lang];
            total = Port_RA_GetAchievementCount();
            unlocked = Port_RA_GetUnlockedCount();
            hardcore = Port_RA_GetHardcoreUnlockedCount();
        } else {
            const RetroAchievementSubset* subset = Port_RA_GetSubset(row - 1u);
            if (!subset) continue;
            name = subset->title;
            total = subset->total;
            unlocked = subset->unlocked;
            hardcore = subset->hardcoreUnlocked;
        }

        bool complete = (total > 0u && unlocked >= total);
        uint32_t borderCol = complete ? C2D_Color32(160, 130, 30, 255) : C2D_Color32(35, 45, 65, 255);
        uint32_t bgCol = complete ? C2D_Color32(40, 36, 16, 255) : C2D_Color32(18, 22, 34, 255);
        DrawRectClipped(16.0f, py, 0.88f, 284.0f, ACH_PACK_CARD_H - 4.0f, borderCol, clipY0, clipY1);
        DrawRectClipped(17.0f, py + 1.0f, 0.89f, 282.0f, ACH_PACK_CARD_H - 6.0f, bgCol, clipY0, clipY1);

        DrawTextMaxWClipped(24.0f, py + 6.0f, 1.0f, name,
                            complete ? C2D_Color32(255, 225, 80, 255) : C2D_Color32(220, 235, 255, 255),
                            clipY0, clipY1, 270.0f);

        char progressBuf[48];
        if (hardcore > 0u) {
            snprintf(progressBuf, sizeof(progressBuf), "%u/%u (%u HC)",
                     (unsigned)unlocked, (unsigned)total, (unsigned)hardcore);
        } else {
            snprintf(progressBuf, sizeof(progressBuf), "%u/%u", (unsigned)unlocked, (unsigned)total);
        }
        DrawTextMaxWClipped(24.0f, py + 20.0f, 1.0f, progressBuf,
                            complete ? C2D_Color32(255, 215, 0, 255) : C2D_Color32(80, 255, 120, 255),
                            clipY0, clipY1, 270.0f);
    }

    if (maxScroll > 0.0f) {
        float trackH = (clipY1 - clipY0);
        float thumbH = 30.0f;
        float thumbY = clipY0 + (sAchPacksScrollY / maxScroll) * (trackH - thumbH);
        C2D_DrawRectSolid(306.0f, clipY0, 0.92f, 2.0f, trackH, C2D_Color32(255, 35, 55, 255));
        C2D_DrawRectSolid(306.0f, thumbY, 0.94f, 2.0f, thumbH, C2D_Color32(80, 160, 240, 255));
    }

    DrawAchSettingsButton(lang);
}

/* Colour RA uses for each special achievement type. */
static uint32_t AchTypeColor(RetroAchievementType type) {
    switch (type) {
        case RA_ACH_TYPE_MISSABLE:      return C2D_Color32(255, 140, 40, 255);
        case RA_ACH_TYPE_PROGRESSION:   return C2D_Color32(90, 160, 255, 255);
        case RA_ACH_TYPE_WIN_CONDITION: return C2D_Color32(210, 120, 255, 255);
        default:                        return C2D_Color32(150, 170, 200, 255);
    }
}

/* Full-word name of a type, for the detail popup. */
static const char* AchTypeName(RetroAchievementType type, int lang) {
    switch (type) {
        case RA_ACH_TYPE_MISSABLE: {
            static const char* l[7] = { "MISSABLE", "MISSABLE", "MISSABLE",
                                        "VERPASSBAR", "MANQUABLE", "MANCABILE", "PERDIBLE" };
            return l[lang];
        }
        case RA_ACH_TYPE_PROGRESSION: {
            static const char* l[7] = { "PROGRESSION", "PROGRESSION", "PROGRESSION",
                                        "FORTSCHRITT", "PROGRESSION", "PROGRESSO", "PROGRESO" };
            return l[lang];
        }
        case RA_ACH_TYPE_WIN_CONDITION: {
            static const char* l[7] = { "BEATEN GAME", "BEATEN GAME", "BEATEN GAME",
                                        "SPIEL BEENDET", "JEU TERMINE", "GIOCO FINITO", "JUEGO TERMINADO" };
            return l[lang];
        }
        default:
            return "";
    }
}

/* The little glyph RA shows on the web for a special achievement type, drawn
 * as pixel rects in a 12x12 box with its top-left at (x, y): an exclamation
 * triangle for missable, rising bars for progression, a chequered finish flag
 * for the "game beaten" win condition. Standard achievements draw nothing. */
static void DrawAchTypeIcon(float x, float y, RetroAchievementType type, bool dim,
                            float depth, float clipY0, float clipY1) {
    if (type != RA_ACH_TYPE_MISSABLE && type != RA_ACH_TYPE_PROGRESSION &&
        type != RA_ACH_TYPE_WIN_CONDITION) {
        return;
    }

    uint32_t col = AchTypeColor(type);
    if (dim) {
        uint32_t r = (col & 0xFF), g = (col >> 8) & 0xFF, b = (col >> 16) & 0xFF;
        col = C2D_Color32((uint8_t)(r / 2 + 30), (uint8_t)(g / 2 + 30), (uint8_t)(b / 2 + 30), 255);
    }
    uint32_t bg = C2D_Color32(12, 16, 24, 255);
    float d2 = depth + 0.005f;

    if (type == RA_ACH_TYPE_MISSABLE) {
        for (int r = 0; r < 6; ++r) {
            DrawRectClipped(x + (5.0f - (float)r), y + (float)r * 2.0f, depth,
                            2.0f + (float)r * 2.0f, 2.0f, col, clipY0, clipY1);
        }
        DrawRectClipped(x + 5.0f, y + 3.0f, d2, 2.0f, 4.0f, bg, clipY0, clipY1);
        DrawRectClipped(x + 5.0f, y + 8.0f, d2, 2.0f, 2.0f, bg, clipY0, clipY1);
    } else if (type == RA_ACH_TYPE_PROGRESSION) {
        DrawRectClipped(x + 0.0f, y + 7.0f, depth, 3.0f, 5.0f, col, clipY0, clipY1);
        DrawRectClipped(x + 4.0f, y + 4.0f, depth, 3.0f, 8.0f, col, clipY0, clipY1);
        DrawRectClipped(x + 8.0f, y + 1.0f, depth, 3.0f, 11.0f, col, clipY0, clipY1);
    } else {
        DrawRectClipped(x + 1.0f, y + 0.0f, depth, 2.0f, 12.0f, col, clipY0, clipY1);
        DrawRectClipped(x + 3.0f, y + 1.0f, depth, 8.0f, 6.0f, col, clipY0, clipY1);
        DrawRectClipped(x + 3.0f, y + 1.0f, d2, 2.0f, 2.0f, bg, clipY0, clipY1);
        DrawRectClipped(x + 7.0f, y + 1.0f, d2, 2.0f, 2.0f, bg, clipY0, clipY1);
        DrawRectClipped(x + 5.0f, y + 3.0f, d2, 2.0f, 2.0f, bg, clipY0, clipY1);
        DrawRectClipped(x + 3.0f, y + 5.0f, d2, 2.0f, 2.0f, bg, clipY0, clipY1);
        DrawRectClipped(x + 7.0f, y + 5.0f, d2, 2.0f, 2.0f, bg, clipY0, clipY1);
    }
}

/* Render RetroAchievements List Modal with Touch Scrolling */
static void RenderAchievementsModal(int lang) {
    const float mX = 10.0f;
    const float mY = 26.0f;
    const float mW = 300.0f;
    const float mH = 206.0f;
    const float clipY0 = ACH_LIST_CLIP_Y0;
    const float clipY1 = ACH_LIST_CLIP_Y1;

    C2D_DrawRectSolid(mX, mY, 0.85f, mW, mH, C2D_Color32(10, 14, 24, 250));
    C2D_DrawRectSolid(mX, mY, 0.84f, mW, mH, C2D_Color32(40, 70, 120, 255));

    /* Header: the set's name when the list is filtered to one, the generic
     * title when it is showing everything. */
    const char* titles[7] = {
        "RETROACHIEVEMENTS LIST", "RETROACHIEVEMENTS LIST", "RETROACHIEVEMENTS LIST",
        "ERFOLGSLISTE", "LISTE DES SUCCES", "LISTA DEGLI OBIETTIVI", "LISTA DE LOGROS"
    };
    const char* header = titles[lang];
    uint32_t filter = Port_RA_GetListSubset();
    if (filter != 0u) {
        for (uint32_t i = 0; i < Port_RA_GetSubsetCount(); ++i) {
            const RetroAchievementSubset* subset = Port_RA_GetSubset(i);
            if (subset && subset->id == filter) { header = subset->title; break; }
        }
    }
    DrawTextMaxWClipped(20.0f, 32.0f, 1.0f, header, C2D_Color32(255, 215, 0, 255), 26.0f, 46.0f, 130.0f);

    /* Counters follow the filter, so a set's card and its list agree. */
    uint32_t count = Port_RA_GetViewCount();
    uint32_t unlocked = 0, hardcore = 0, totalPts = 0, unlPts = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const RetroAchievementItem* ach = Port_RA_GetViewAchievement(i);
        if (!ach) continue;
        totalPts += ach->points;
        if (ach->unlocked) { ++unlocked; unlPts += ach->points; }
        if (ach->hardcoreUnlocked) ++hardcore;
    }

    char summaryBuf[64];
    if (hardcore > 0) {
        snprintf(summaryBuf, sizeof(summaryBuf), "%u/%u (%u HC) %uP", (unsigned)unlocked, (unsigned)count, (unsigned)hardcore, (unsigned)unlPts);
    } else {
        snprintf(summaryBuf, sizeof(summaryBuf), "%u/%u (%u/%u PTS)", (unsigned)unlocked, (unsigned)count, (unsigned)unlPts, (unsigned)totalPts);
    }
    DrawText(156.0f, 32.0f, 1.0f, summaryBuf, hardcore > 0 ? C2D_Color32(255, 215, 0, 255) : C2D_Color32(80, 255, 120, 255));

    float cardH = ACH_CARD_H;
    float maxAchScroll = AchievementsMaxScroll();
    if (sAchievementsScrollY > maxAchScroll) sAchievementsScrollY = maxAchScroll;

    for (uint32_t i = 0; i < count; ++i) {
        const RetroAchievementItem* ach = Port_RA_GetViewAchievement(i);
        if (!ach) continue;

        float py = clipY0 - sAchievementsScrollY + (float)i * cardH;
        if (py + cardH <= clipY0 || py >= clipY1) continue;

        /* Card background */
        uint32_t bgCol = ach->hardcoreUnlocked ? C2D_Color32(40, 36, 16, 255) :
                         (ach->unlocked ? C2D_Color32(16, 38, 26, 255) : C2D_Color32(18, 22, 34, 255));
        uint32_t borderCol = ach->hardcoreUnlocked ? C2D_Color32(160, 130, 30, 255) :
                             (ach->unlocked ? C2D_Color32(35, 120, 65, 255) : C2D_Color32(35, 45, 65, 255));

        DrawRectClipped(16.0f, py, 0.88f, 284.0f, cardH - 2.0f, borderCol, clipY0, clipY1);
        DrawRectClipped(17.0f, py + 1.0f, 0.89f, 282.0f, cardH - 4.0f, bgCol, clipY0, clipY1);

        /* 1. Badge / Icon on Left (X: 19, Y: py + 3, W: 24, H: 24) */
        float iconX = 19.0f;
        float iconY = py + 3.0f;
        if (iconY + 24.0f > clipY0 && iconY < clipY1) {
            uint32_t iconBorder = ach->hardcoreUnlocked ? C2D_Color32(255, 215, 0, 255) :
                                  (ach->unlocked ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(60, 75, 100, 255));
            DrawRectClipped(iconX, iconY, 0.91f, 24.0f, 24.0f, iconBorder, clipY0, clipY1);
            DrawRectClipped(iconX + 1.0f, iconY + 1.0f, 0.92f, 22.0f, 22.0f, C2D_Color32(12, 16, 24, 255), clipY0, clipY1);

            const uint32_t* badgePixels = Port_RA_GetBadgePixels(ach->badgeName);
            if (badgePixels) {
                float badgeX = iconX + 2.0f;
                float badgeY = iconY + 2.0f;
                for (int by = 0; by < 20; ++by) {
                    float rY = badgeY + (float)by;
                    if (rY < clipY0 || rY >= clipY1) continue;
                    for (int bx = 0; bx < 20; ++bx) {
                        uint32_t pColor = badgePixels[by * 20 + bx];
                        if (!ach->unlocked && !ach->hardcoreUnlocked) {
                            uint32_t r = pColor & 0xFF;
                            uint32_t g = (pColor >> 8) & 0xFF;
                            uint32_t b = (pColor >> 16) & 0xFF;
                            uint32_t gray = (r * 30 + g * 59 + b * 11) / 250;
                            pColor = C2D_Color32((uint8_t)gray, (uint8_t)gray, (uint8_t)gray, 255);
                        }
                        C2D_DrawRectSolid(badgeX + (float)bx, rY, 0.93f, 1.0f, 1.0f, pColor);
                    }
                }
            } else {
                if (ach->hardcoreUnlocked) {
                    DrawRectClipped(iconX + 4.0f, iconY + 4.0f, 0.93f, 14.0f, 14.0f, C2D_Color32(255, 200, 50, 255), clipY0, clipY1);
                    DrawTextMaxWClipped(iconX + 8.0f, iconY + 7.0f, 1.0f, "H", C2D_Color32(20, 20, 20, 255), clipY0, clipY1, 8.0f);
                } else if (ach->unlocked) {
                    DrawRectClipped(iconX + 4.0f, iconY + 4.0f, 0.93f, 14.0f, 14.0f, C2D_Color32(60, 200, 100, 255), clipY0, clipY1);
                    DrawTextMaxWClipped(iconX + 8.0f, iconY + 7.0f, 1.0f, "*", C2D_Color32(255, 255, 255, 255), clipY0, clipY1, 8.0f);
                } else {
                    DrawRectClipped(iconX + 4.0f, iconY + 4.0f, 0.93f, 14.0f, 14.0f, C2D_Color32(40, 50, 70, 255), clipY0, clipY1);
                    DrawTextMaxWClipped(iconX + 8.0f, iconY + 7.0f, 1.0f, "?", C2D_Color32(120, 140, 170, 255), clipY0, clipY1, 8.0f);
                }
            }
        }

        /* 2. Graphical Status Indicator on Top-Right */
        float statX = 286.0f;
        float statY = py + 4.0f;
        if (statY + 8.0f > clipY0 && statY < clipY1) {
            if (ach->hardcoreUnlocked) {
                /* Hardcore Mini Crown (Gold) */
                uint32_t cGold = C2D_Color32(255, 215, 0, 255);
                DrawRectClipped(statX, statY + 3.0f, 0.93f, 8.0f, 4.0f, cGold, clipY0, clipY1);
                DrawRectClipped(statX + 1.0f, statY + 1.0f, 0.93f, 2.0f, 2.0f, cGold, clipY0, clipY1);
                DrawRectClipped(statX + 3.0f, statY, 0.93f, 2.0f, 2.0f, cGold, clipY0, clipY1);
                DrawRectClipped(statX + 5.0f, statY + 1.0f, 0.93f, 2.0f, 2.0f, cGold, clipY0, clipY1);
            } else if (ach->unlocked) {
                /* Softcore Unlocked Mini Padlock Open / Checkmark (Green) */
                uint32_t cGreen = C2D_Color32(80, 255, 120, 255);
                /* Open shackle */
                DrawRectClipped(statX + 3.0f, statY, 0.93f, 4.0f, 1.0f, cGreen, clipY0, clipY1);
                DrawRectClipped(statX + 6.0f, statY + 1.0f, 0.93f, 1.0f, 2.0f, cGreen, clipY0, clipY1);
                /* Body */
                DrawRectClipped(statX + 1.0f, statY + 3.0f, 0.93f, 6.0f, 4.0f, cGreen, clipY0, clipY1);
            } else {
                /* Locked Mini Padlock Closed (Slate Gray) */
                uint32_t cLock = C2D_Color32(110, 130, 160, 255);
                /* Closed shackle */
                DrawRectClipped(statX + 2.0f, statY, 0.93f, 4.0f, 1.0f, cLock, clipY0, clipY1);
                DrawRectClipped(statX + 2.0f, statY + 1.0f, 0.93f, 1.0f, 2.0f, cLock, clipY0, clipY1);
                DrawRectClipped(statX + 5.0f, statY + 1.0f, 0.93f, 1.0f, 2.0f, cLock, clipY0, clipY1);
                /* Body */
                DrawRectClipped(statX + 1.0f, statY + 3.0f, 0.93f, 6.0f, 4.0f, cLock, clipY0, clipY1);
            }
        }

        /* 3. Title on the top line, full width now that the points have moved
         * off it -- a long title no longer has to fight the "(NNP)" suffix for
         * the last few pixels. */
        uint32_t titleCol = ach->hardcoreUnlocked ? C2D_Color32(255, 225, 80, 255) :
                            (ach->unlocked ? C2D_Color32(140, 240, 170, 255) : C2D_Color32(220, 235, 255, 255));
        DrawTextMaxWClipped(48.0f, py + 4.0f, 1.0f, ach->title, titleCol, clipY0, clipY1, 236.0f);

        /* 4. Points, bottom-left: directly under the title and beside the
         * badge, in the space that was otherwise empty. */
        char ptsBuf[16];
        snprintf(ptsBuf, sizeof(ptsBuf), "%uP", (unsigned)ach->points);
        uint32_t ptsCol = ach->hardcoreUnlocked ? C2D_Color32(255, 215, 0, 255) :
                          (ach->unlocked ? C2D_Color32(120, 255, 160, 255) : C2D_Color32(140, 160, 190, 255));
        if (py + 18.0f + 7.0f > clipY0 && py + 18.0f < clipY1) {
            DrawTextClipped(48.0f, py + 18.0f, 1.0f, ptsBuf, ptsCol, clipY0, clipY1);
        }

        /* 5. Type icon (missable / progression / beaten-game), lower-right of
         * the card, as on the RA website. Dimmed while still locked. */
        if (py + 16.0f + 12.0f > clipY0 && py + 16.0f < clipY1) {
            DrawAchTypeIcon(285.0f, py + 16.0f, ach->type,
                            !ach->unlocked && !ach->hardcoreUnlocked, 0.93f, clipY0, clipY1);
        }
    }

    /* Achievements modal scrollbar */
    if (maxAchScroll > 0.0f) {
        float trackH = (clipY1 - clipY0);
        float thumbH = 30.0f;
        float thumbY = clipY0 + (sAchievementsScrollY / maxAchScroll) * (trackH - thumbH);
        C2D_DrawRectSolid(306.0f, clipY0, 0.92f, 2.0f, trackH, C2D_Color32(255, 35, 55, 255));
        C2D_DrawRectSolid(306.0f, thumbY, 0.94f, 2.0f, thumbH, C2D_Color32(80, 160, 240, 255));
    }

    DrawAchSettingsButton(lang);

    if (sAchFromPacks) {
        const char* backLabels[7] = {
            "BACK", "BACK", "BACK",
            "ZURUECK", "RETOUR", "INDIETRO", "VOLVER"
        };
        DrawButton(116.0f, 204.0f, 88.0f, 20.0f, backLabels[lang],
                   C2D_Color32(255, 255, 255, 255), BTN_BLUE_BODY, BTN_BLUE_BORDER);
    }

    /* Sort cycler plus its direction arrow, on the footer bar beside BACK so
     * neither can collide with the scrolling card band above them. */
    /* Single line -- the two-line "SORT" + value never fit this 20px box and
     * the caption clipped the top. The value alone plus the arrow next to it
     * says enough. */
    DrawButton(206.0f, 204.0f, 78.0f, 20.0f, AchSortLabel(lang),
               C2D_Color32(190, 220, 255, 255),
               C2D_Color32(24, 40, 70, 255), C2D_Color32(70, 110, 170, 255));

    /* Direction toggle: a chevron pointing down for descending (Z-A, most
     * points first, newest first) and up for ascending. Drawn as stacked
     * rows because this UI has no glyph for it. */
    bool descending = Port_RA_GetListDescending();
    DrawButtonBox(288.0f, 204.0f, 18.0f, 20.0f,
                  C2D_Color32(24, 40, 70, 255), C2D_Color32(70, 110, 170, 255));
    for (int row = 0; row < 4; ++row) {
        /* Widest row at the flat end, narrowest at the point. */
        int step = descending ? (3 - row) : row;
        float w = 2.0f + (float)step * 2.0f;
        float px = 297.0f - w * 0.5f;
        float py = 210.0f + (float)row * 2.0f;
        C2D_DrawRectSolid(px, py, 0.97f, w, 2.0f, C2D_Color32(150, 200, 255, 255));
    }
}

/* Read-only detail popup for one achievement, opened by tapping its card in
 * the list. Shows the full badge, the untruncated title and description, the
 * point value, the lock state (with unlock date when known) and the type. */
static void RenderAchievementDetailModal(int lang) {
    const RetroAchievementItem* a = &sAchDetail;
    const float pX = 6.0f, pY = 20.0f, pW = 308.0f, pH = 214.0f;
    const float clip0 = pY, clip1 = pY + pH;

    C2D_DrawRectSolid(pX, pY, 0.975f, pW, pH, C2D_Color32(255, 215, 0, 255));
    C2D_DrawRectSolid(pX + 2.0f, pY + 2.0f, 0.978f, pW - 4.0f, pH - 4.0f, C2D_Color32(8, 11, 20, 252));

    /* Badge, nearest-neighbour scaled x4. Greyed while still locked, matching
     * the list. */
    const float px = 4.0f;
    float bX = pX + 12.0f, bY = pY + 12.0f;
    bool locked = !a->unlocked && !a->hardcoreUnlocked;
    C2D_DrawRectSolid(bX - 2.0f, bY - 2.0f, 0.979f, 20.0f * px + 4.0f, 20.0f * px + 4.0f,
                      C2D_Color32(60, 75, 100, 255));
    const uint32_t* badge = Port_RA_GetBadgePixels(a->badgeName);
    if (badge) {
        for (int y = 0; y < 20; ++y) {
            for (int x = 0; x < 20; ++x) {
                uint32_t c = badge[y * 20 + x];
                if (locked) {
                    uint32_t r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
                    uint32_t gray = (r * 30 + g * 59 + b * 11) / 250;
                    c = C2D_Color32((uint8_t)gray, (uint8_t)gray, (uint8_t)gray, 255);
                }
                C2D_DrawRectSolid(bX + (float)x * px, bY + (float)y * px, 0.98f, px, px, c);
            }
        }
    } else {
        C2D_DrawRectSolid(bX, bY, 0.98f, 20.0f * px, 20.0f * px, C2D_Color32(20, 26, 40, 255));
    }

    float tX = bX + 20.0f * px + 10.0f;
    float tW = pX + pW - 12.0f - tX;
    DrawWrappedTextClipped(tX, pY + 12.0f, 1.0f, a->title, tW, 11.0f,
                           C2D_Color32(255, 230, 120, 255), clip0, clip1);

    char buf[96];
    snprintf(buf, sizeof(buf), "%u PTS", (unsigned)a->points);
    DrawText(tX, pY + 46.0f, 1.0f, buf, C2D_Color32(120, 255, 160, 255));

    static const char* const stHardcore[7] = {
        "UNLOCKED (HARDCORE)", "UNLOCKED (HARDCORE)", "UNLOCKED (HARDCORE)",
        "FREIGESCHALTET (HARDCORE)", "DEBLOQUE (HARDCORE)", "SBLOCCATO (HARDCORE)",
        "DESBLOQUEADO (HARDCORE)"
    };
    static const char* const stUnlocked[7] = {
        "UNLOCKED", "UNLOCKED", "UNLOCKED",
        "FREIGESCHALTET", "DEBLOQUE", "SBLOCCATO", "DESBLOQUEADO"
    };
    static const char* const stLocked[7] = {
        "LOCKED", "LOCKED", "LOCKED",
        "GESPERRT", "VERROUILLE", "BLOCCATO", "BLOQUEADO"
    };
    const char* stTxt;
    uint32_t stCol;
    if (a->hardcoreUnlocked)  { stTxt = stHardcore[lang]; stCol = C2D_Color32(255, 215, 0, 255); }
    else if (a->unlocked)     { stTxt = stUnlocked[lang]; stCol = C2D_Color32(80, 255, 120, 255); }
    else                      { stTxt = stLocked[lang];   stCol = C2D_Color32(150, 170, 200, 255); }
    DrawText(tX, pY + 60.0f, 1.0f, stTxt, stCol);

    if (!locked && a->unlockTime != 0) {
        time_t t = (time_t)a->unlockTime;
        struct tm* tmv = gmtime(&t);
        if (tmv && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", tmv) > 0) {
            DrawText(tX, pY + 74.0f, 1.0f, buf, C2D_Color32(150, 180, 210, 255));
        }
    }

    /* Type row, using the same glyph the list card carries. */
    if (a->type == RA_ACH_TYPE_MISSABLE || a->type == RA_ACH_TYPE_PROGRESSION ||
        a->type == RA_ACH_TYPE_WIN_CONDITION) {
        float ty = pY + 96.0f;
        DrawAchTypeIcon(bX + 2.0f, ty, a->type, false, 0.985f, clip0, clip1);
        DrawText(bX + 20.0f, ty + 2.0f, 1.0f, AchTypeName(a->type, lang), AchTypeColor(a->type));
    }

    DrawWrappedTextClipped(pX + 12.0f, pY + 118.0f, 1.0f,
                           a->description[0] ? a->description : "(NO DESCRIPTION)",
                           pW - 24.0f, 12.0f, C2D_Color32(220, 235, 255, 255), clip0, clip1 - 26.0f);

    {
        static const char* backLabels[7] = {
            "BACK", "BACK", "BACK", "ZURUECK", "RETOUR", "INDIETRO", "VOLVER"
        };
        DrawButton(116.0f, 208.0f, 88.0f, 20.0f, backLabels[lang],
                   C2D_Color32(255, 255, 255, 255), BTN_BLUE_BODY, BTN_BLUE_BORDER);
    }
}

/* Render Confirmation Dialog (hardcore enable / restart game) */
static void RenderConfirmModal(int lang) {
    C2D_DrawRectSolid(10.0f, 26.0f, 0.85f, 300.0f, 206.0f, C2D_Color32(10, 14, 24, 250));
    C2D_DrawRectSolid(10.0f, 26.0f, 0.84f, 300.0f, 206.0f, C2D_Color32(40, 70, 120, 255));

    static const char* const restartTitles[7] = {
        "RESTART GAME", "RESTART GAME", "RESTART GAME",
        "SPIEL NEUSTARTEN", "RECOMMENCER PARTIE", "RIAVVIA PARTITA", "REINICIAR PARTIDA"
    };
    static const char* const hcTitles[7] = {
        "HARDCORE MODE", "HARDCORE MODE", "HARDCORE MODE",
        "HARDCORE-MODUS", "MODE HARDCORE", "MODO HARDCORE", "MODO HARDCORE"
    };
    const char* title = sConfirmIsRestart ? restartTitles[lang] : hcTitles[lang];
    DrawTextCentered(160.0f, 60.0f, 1.0f, title, C2D_Color32(255, 215, 0, 255));

    static const char* const restartMsgs[7] = {
        "THE GAME WILL BE RESTARTED. ALL UNSAVED PROGRESS WILL BE LOST. CONTINUE?",
        "THE GAME WILL BE RESTARTED. ALL UNSAVED PROGRESS WILL BE LOST. CONTINUE?",
        "THE GAME WILL BE RESTARTED. ALL UNSAVED PROGRESS WILL BE LOST. CONTINUE?",
        "DAS SPIEL WIRD NEU GESTARTET. NICHT GESPEICHERTER FORTSCHRITT GEHT VERLOREN. WEITER?",
        "LA PARTIE VA RECOMMENCER. TOUTE PROGRESSION NON SAUVEGARDEE SERA PERDUE. CONTINUER?",
        "LA PARTITA VERRA RIAVVIATA. TUTTI I PROGRESSI NON SALVATI ANDRANNO PERSI. CONTINUARE?",
        "SE REINICIARA LA PARTIDA. TODO EL PROGRESO NO GUARDADO SE PERDERA. CONTINUAR?"
    };
    static const char* const hcMsgs[7] = {
        "ENABLING HARDCORE MODE WILL RESTART THE GAME FROM THE BEGINNING. CONTINUE?",
        "ENABLING HARDCORE MODE WILL RESTART THE GAME FROM THE BEGINNING. CONTINUE?",
        "ENABLING HARDCORE MODE WILL RESTART THE GAME FROM THE BEGINNING. CONTINUE?",
        "DAS AKTIVIEREN DES HARDCORE-MODUS STARTET DAS SPIEL VON VORN. WEITER?",
        "ACTIVER LE MODE HARDCORE RECOMMENCERA LE JEU DEPUIS LE DEBUT. CONTINUER?",
        "ATTIVARE LA MODALITA HARDCORE RIAVVIERA IL GIOCO DALL'INIZIO. CONTINUARE?",
        "AL ACTIVAR EL MODO HARDCORE LA PARTIDA SE REINICIARA DESDE EL PRINCIPIO. CONTINUAR?"
    };
    const char* msg = sConfirmIsRestart ? restartMsgs[lang] : hcMsgs[lang];
    DrawWrappedTextClipped(30.0f, 84.0f, 1.0f, msg, 260.0f, 11.0f, C2D_Color32(220, 235, 255, 255), 50.0f, 136.0f);

    /* ACCEPT button */
    static const char* const acceptLabels[7] = {
        "ACCEPT", "ACCEPT", "ACCEPT",
        "JA", "ACCEPTER", "ACCETTA", "ACEPTAR"
    };
    DrawButton(40.0f, 140.0f, 110.0f, 28.0f, acceptLabels[lang], C2D_Color32(255, 255, 255, 255),
               C2D_Color32(20, 90, 45, 255), C2D_Color32(70, 180, 100, 255));

    /* CANCEL button */
    static const char* const cancelModalLabels[7] = {
        "CANCEL", "CANCEL", "CANCEL",
        "NEIN", "ANNULER", "ANNULLA", "CANCELAR"
    };
    DrawButton(170.0f, 140.0f, 110.0f, 28.0f, cancelModalLabels[lang], C2D_Color32(255, 255, 255, 255),
               C2D_Color32(110, 30, 30, 255), C2D_Color32(200, 80, 80, 255));
}

/* Two-column grid for the DISPLAY modal. Same geometry as the debug-tools
 * grid (DBGTOOL_*), but defined here because that block only compiles in
 * PORT_DEBUG_TOOLS_ACTIVE builds and this modal also ships in release. */
#define DISP_COL_L_X    16
#define DISP_COL_R_X    164
#define DISP_COL_W      140
#define DISP_GRID_Y0    46
#define DISP_GRID_PITCH 26
#define DISP_CELL_H     24
#define DISP_GRID_ROWS  6
#define DISP_CELL_COUNT 12

/* One label line plus a value/state line under it, tinted by `valueCol`. */
static void DispCell(int index, const char* label, const char* value, uint32_t valueCol) {
    float x = (index & 1) ? (float)DISP_COL_R_X : (float)DISP_COL_L_X;
    float y = (float)(DISP_GRID_Y0 + (index >> 1) * DISP_GRID_PITCH);
    DrawButtonBox(x, y, (float)DISP_COL_W, (float)DISP_CELL_H,
                  C2D_Color32(24, 32, 50, 255), C2D_Color32(50, 80, 130, 255));
    DrawTextMaxWClipped(x + 6.0f, y + 4.0f, 1.0f, label, C2D_Color32(255, 255, 255, 255),
                        0.0f, 240.0f, (float)DISP_COL_W - 12.0f);
    if (value) DrawTextMaxWClipped(x + 6.0f, y + 14.0f, 1.0f, value, valueCol,
                                   0.0f, 240.0f, (float)DISP_COL_W - 12.0f);
}

/* Index of the grid cell a tap landed on, or -1. */
static int DispCellHit(int x, int y) {
    int col;
    if (x >= DISP_COL_L_X && x <= DISP_COL_L_X + DISP_COL_W) col = 0;
    else if (x >= DISP_COL_R_X && x <= DISP_COL_R_X + DISP_COL_W) col = 1;
    else return -1;
    for (int r = 0; r < DISP_GRID_ROWS; ++r) {
        int cy = DISP_GRID_Y0 + r * DISP_GRID_PITCH;
        if (y < cy || y > cy + DISP_CELL_H) continue;
        int idx = r * 2 + col;
        return (idx < DISP_CELL_COUNT) ? idx : -1;
    }
    return -1;
}

/* OFF / LOW / MEDIUM / HIGH label for a GBA-effect level (0..3). */
static const char* GbaFxLevelName(int lang, int level) {
    switch (level) {
        case 1:  return (lang == 6) ? "BAJO" : "LOW";
        case 2:  return (lang == 6) ? "MEDIO" : "MED";
        case 3:  return (lang == 6) ? "ALTO" : "HIGH";
        default: return (lang == 6) ? "NO" : "OFF";
    }
}

/* Render Display Settings Modal -- two-column grid so the GBA-look effect
 * toggles fit alongside the existing rows without scrolling. */
static void RenderDisplayModal(int lang) {
    C2D_DrawRectSolid(10.0f, 26.0f, 0.85f, 300.0f, 206.0f, C2D_Color32(10, 14, 24, 250));
    C2D_DrawRectSolid(10.0f, 26.0f, 0.84f, 300.0f, 206.0f, C2D_Color32(40, 70, 120, 255));

    static const char* const titles[7] = {
        "DISPLAY SETTINGS", "DISPLAY SETTINGS", "DISPLAY SETTINGS",
        "BILDSCHIRMEINSTELLUNGEN", "PARAMETRES D'AFFICHAGE", "IMPOSTAZIONI SCHERMO", "CONFIGURACION DE PANTALLA"
    };
    DrawText(20.0f, 30.0f, 1.0f, titles[lang], C2D_Color32(255, 215, 0, 255));

    const uint32_t valCol  = C2D_Color32(255, 215, 0, 255);
    const uint32_t onCol   = C2D_Color32(80, 255, 120, 255);
    const uint32_t offCol  = C2D_Color32(255, 100, 100, 255);
    const uint32_t idleCol = C2D_Color32(150, 150, 150, 255);

    const char* onTxt  = (lang == 6) ? "ACTIVADO" : ((lang == 3) ? "EIN" : ((lang == 4) ? "ACTIVE" : ((lang == 5) ? "ATTIVO" : "ON")));
    const char* offTxt = (lang == 6) ? "DESACT." : ((lang == 3) ? "AUS" : ((lang == 4) ? "DESACT." : ((lang == 5) ? "DISATT." : "OFF")));

    static const char* const langL[7]  = { "LANGUAGE","LANGUAGE","LANGUAGE","SPRACHE","LANGUE","LINGUA","IDIOMA" };
    static const char* const aspL[7]   = { "ASPECT","ASPECT","ASPECT","BILDFORMAT","FORMAT","FORMATO","ASPECTO" };
    static const char* const styL[7]   = { "DISPLAY STYLE","DISPLAY STYLE","DISPLAY STYLE","DARSTELLUNG","STYLE","STILE","ESTILO" };
    static const char* const fpsL[7]   = { "SHOW FPS","SHOW FPS","SHOW FPS","FPS ANZEIGEN","AFFICHER FPS","MOSTRA FPS","MOSTRAR FPS" };
    static const char* const hudL[7]   = { "AUTO-HIDE HUD","AUTO-HIDE HUD","AUTO-HIDE HUD","HUD AUTO-AUS","MASQUER HUD","NASCONDI HUD","AUTOOCULTAR HUD" };
    static const char* const spoL[7]   = { "HIDE SPOILERS","HIDE SPOILERS","HIDE SPOILERS","SPOILER AUS","MASQ. SPOILERS","NASC. SPOILER","OCULTAR SPOILERS" };

    DispCell(0, langL[lang], Port_Config_GetLanguageDisplayName(lang), valCol);
    bool wideOn = (Port_Config_Get3DSAspectRatio() == 3);
    DispCell(1, aspL[lang], GetAspectRatioDisplayName(lang), wideOn ? onCol : valCol);
    DispCell(2, styL[lang], GetDisplayStyleDisplayName(lang), valCol);

    bool fpsOn = Port_Config_GetShowFps();
    DispCell(3, fpsL[lang], GetFpsOverlayDisplayName(lang), fpsOn ? onCol : offCol);
    bool ahOn = Port_Config_GetAutoHideHud();
    DispCell(4, hudL[lang], ahOn ? onTxt : offTxt, ahOn ? onCol : offCol);
    bool spOn = Port_Config_GetHideSpoilers();
    DispCell(5, spoL[lang], spOn ? onTxt : offTxt, spOn ? onCol : offCol);

    static const char* const gradeL[7] = { "GBA COLOR","GBA COLOR","GBA COLOR","GBA-FARBE","COULEUR GBA","COLORE GBA","COLOR GBA" };
    static const char* const gridL[7]  = { "GBA GRID","GBA GRID","GBA GRID","GBA-GITTER","GRILLE GBA","GRIGLIA GBA","REJILLA GBA" };
    static const char* const vigL[7]   = { "VIGNETTE","VIGNETTE","VIGNETTE","VIGNETTE","VIGNETTAGE","VIGNETTA","VINETA" };

    int fx0 = Port_Config_GetGbaFxGrade();
    int fx1 = Port_Config_GetGbaFxGrid();
    int fx2 = Port_Config_GetGbaFxVignette();
    DispCell(6, gradeL[lang], GbaFxLevelName(lang, fx0), fx0 ? onCol : idleCol);
    DispCell(7, gridL[lang],  GbaFxLevelName(lang, fx1), fx1 ? onCol : idleCol);
    DispCell(8, vigL[lang],   GbaFxLevelName(lang, fx2), fx2 ? onCol : idleCol);

    static const char* const hudPosL[7] = { "HUD POSITION","HUD POSITION","HUD POSITION","HUD-POSITION","POSITION HUD","POSIZIONE HUD","POSICION HUD" };
    static const char* const bezelL[7]  = { "GBA FRAME","GBA FRAME","GBA FRAME","GBA-RAHMEN","CADRE GBA","CORNICE GBA","MARCO GBA" };

    const char* lockedTxt = (lang == 6) ? "BLOQUEADO" : "LOCKED";
    bool pixelPerfect = (Port_Config_Get3DSDisplayStyle() == 0);
    /* WIDE fills the border with world, leaving no room for a bezel or for
     * the HUD to move into. */
    const bool wide = (Port_Config_Get3DSAspectRatio() == 3);

    /* HUD Position: TOP BORDER vs DEFAULT (only active in Pixel Perfect) */
    bool hudOut = Port_Config_GetHudOutside();
    const char* hudOutTxt = (lang == 6) ? "BORDE SUP." : ((lang == 3) ? "OBEN" : ((lang == 4) ? "BORD SUP." : ((lang == 5) ? "BORDO SUP." : "TOP BORDER")));
    const char* hudDefTxt = (lang == 6) ? "NORMAL" : ((lang == 3) ? "NORMAL" : ((lang == 4) ? "NORMAL" : ((lang == 5) ? "NORMALE" : "DEFAULT")));
    DispCell(9, hudPosL[lang],
             (pixelPerfect && !wide) ? (hudOut ? hudOutTxt : hudDefTxt) : lockedTxt,
             (pixelPerfect && !wide) ? (hudOut ? onCol : valCol) : idleCol);

    /* GBA Bezel: ON vs OFF (active in Pixel Perfect or Scaled Original) */
    bool bezelAllowed = !wide && (pixelPerfect || Port_Config_Get3DSAspectRatio() == 1);
    bool bezelOn = Port_Config_GetGbaBezel();
    DispCell(10, bezelL[lang],
             bezelAllowed ? (bezelOn ? onTxt : offTxt) : lockedTxt,
             bezelAllowed ? (bezelOn ? onCol : offCol) : idleCol);

    /* Frame pacing: ADAPTIVE (variable 30..60, fluid) vs LOCKED 30 (steady).
     * There is no 60 option -- the renderer never exceeds the vblank. */
    static const char* const paceL[7] = { "FRAME PACING","FRAME PACING","FRAME PACING",
                                          "BILDRATE","CADENCE","CADENZA","RITMO FPS" };
    int pace = Port_Config_GetFramePacing();
    const char* paceTxt = (pace == 1) ? ((lang == 6) ? "30 FIJO" : "LOCK 30")
                                      : ((lang == 6) ? "ADAPTABLE" : "ADAPTIVE");
    DispCell(11, paceL[lang], paceTxt, pace ? valCol : onCol);

    /* Close button (Y: 206 to 228) */
    static const char* const closeLabels[7] = {
        "CLOSE", "CLOSE", "CLOSE",
        "SCHLIESSEN", "FERMER", "CHIUDI", "CERRAR"
    };
    DrawButton(116.0f, 206.0f, 88.0f, 22.0f, closeLabels[lang],
               C2D_Color32(255, 255, 255, 255), BTN_BLUE_BODY, BTN_BLUE_BORDER);
}

/* Render RetroAchievements Settings Modal */
static void RenderRASettingsModal(int lang) {
    C2D_DrawRectSolid(10.0f, 26.0f, 0.85f, 300.0f, 206.0f, C2D_Color32(10, 14, 24, 250));
    C2D_DrawRectSolid(10.0f, 26.0f, 0.84f, 300.0f, 206.0f, C2D_Color32(40, 70, 120, 255));

    const char* raTitles[7] = {
        "RETROACHIEVEMENTS SETTINGS", "RETROACHIEVEMENTS SETTINGS", "RETROACHIEVEMENTS SETTINGS",
        "RETROACHIEVEMENTS EINSTELLUNGEN", "PARAMETRES RETROACHIEVEMENTS", "IMPOSTAZIONI RETROACHIEVEMENTS", "AJUSTES DE RETROACHIEVEMENTS"
    };
    DrawText(20.0f, 32.0f, 1.0f, raTitles[lang], C2D_Color32(255, 215, 0, 255));

    /* Status indicator text top-right */
    uint32_t statusCol = C2D_Color32(140, 160, 190, 255);
    switch (Port_RA_GetStatus()) {
        case RA_STATUS_CONNECTED: statusCol = C2D_Color32(80, 255, 120, 255); break;
        case RA_STATUS_CONNECTING: statusCol = C2D_Color32(255, 220, 80, 255); break;
        case RA_STATUS_ERROR: statusCol = C2D_Color32(255, 90, 90, 255); break;
        default: break;
    }
    DrawText(200.0f, 32.0f, 1.0f, Port_RA_GetStatusString(lang), statusCol);

    /* All four rows share one geometry: a button-styled box, the label at
     * x+8, the value at x=170, both vertically centred. Row 3 also carries a
     * square play sub-button flush to the right edge. */
    const uint32_t rowBody = C2D_Color32(24, 32, 50, 255);
    const uint32_t rowBorder = C2D_Color32(60, 90, 130, 255);

    /* Row 1: User Login (Y: 56 to 80) */
    DrawButtonBox(16.0f, 56.0f, 288.0f, 24.0f, rowBody, rowBorder);
    const char* userLabels[7] = {
        "USER:", "USER:", "USER:",
        "BENUTZER:", "UTILISATEUR:", "UTENTE:", "USUARIO:"
    };
    DrawText(24.0f, 65.0f, 1.0f, userLabels[lang], C2D_Color32(255, 255, 255, 255));
    const char* user = Port_RA_GetUsername();
    if (user && user[0] != '\0') {
        DrawText(170.0f, 65.0f, 1.0f, user, C2D_Color32(255, 215, 0, 255));
    } else {
        const char* loginLabels[7] = {
            "LOGIN", "LOGIN", "LOGIN",
            "ANMELDEN", "CONNEXION", "ACCEDI", "INICIAR SESION"
        };
        DrawText(170.0f, 65.0f, 1.0f, loginLabels[lang], C2D_Color32(255, 140, 80, 255));
    }

    /* Row 2: Enable / Disable (Y: 84 to 108) */
    DrawButtonBox(16.0f, 84.0f, 288.0f, 24.0f, rowBody, rowBorder);
    const char* sysLabels[7] = {
        "ACHIEVEMENTS SYS:", "ACHIEVEMENTS SYS:", "ACHIEVEMENTS SYS:",
        "ERFOLGE-SYSTEM:", "SYSTEME SUCCES:", "SISTEMA OBIETTIVI:", "SISTEMA LOGROS:"
    };
    DrawText(24.0f, 93.0f, 1.0f, sysLabels[lang], C2D_Color32(255, 255, 255, 255));
    bool raEn = Port_RA_IsEnabled();
    const char* onStr = (lang == 6) ? "ACTIVADO" : ((lang == 3) ? "AKTIVIERT" : ((lang == 4) ? "ACTIVE" : ((lang == 5) ? "ATTIVO" : "ENABLED")));
    const char* offStr = (lang == 6) ? "DESACTIVADO" : ((lang == 3) ? "DEAKTIVIERT" : ((lang == 4) ? "DESACTIVE" : ((lang == 5) ? "DISATTIVO" : "DISABLED")));
    DrawText(170.0f, 93.0f, 1.0f, raEn ? onStr : offStr,
             raEn ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(255, 100, 100, 255));

    /* Row 3: Toast notification screen -- bottom or top (Y: 112 to 136).
     * This slot used to be the Hardcore Mode toggle; hardcore is forced off
     * now (Port_RA_HardcoreAllowed always false) so the row is repurposed.
     * The play sub-button fires a sample toast (+ jingle). */
    DrawButtonBox(16.0f, 112.0f, 288.0f, 24.0f, rowBody, rowBorder);
    const char* scrLabels[7] = {
        "NOTIFY SCREEN:", "NOTIFY SCREEN:", "NOTIFY SCREEN:",
        "MELDUNG:", "ECRAN NOTIF:", "SCHERMO:", "PANTALLA AVISO:"
    };
    DrawText(24.0f, 121.0f, 1.0f, scrLabels[lang], C2D_Color32(255, 255, 255, 255));
    bool notifTop = Port_RA_GetNotifyOnTopScreen();
    const char* topLabels[7] = { "TOP", "TOP", "TOP", "OBEN", "HAUT", "ALTO", "SUPERIOR" };
    const char* botLabels[7] = { "BOTTOM", "BOTTOM", "BOTTOM", "UNTEN", "BAS", "BASSO", "INFERIOR" };
    DrawText(170.0f, 121.0f, 1.0f, notifTop ? topLabels[lang] : botLabels[lang],
             C2D_Color32(120, 200, 255, 255));
    {
        bool pp = DrawButtonBox(276.0f, 114.0f, 28.0f, 20.0f, BTN_BLUE_BODY, BTN_BLUE_BORDER);
        DrawActionGlyph(290.0f, 124.0f + (pp ? 1.0f : 0.0f), false, C2D_Color32(255, 255, 255, 255));
    }

    /* Row 4: Achievement Notification Sound (Y: 140 to 164) */
    DrawButtonBox(16.0f, 140.0f, 288.0f, 24.0f, rowBody, rowBorder);
    const char* sndLabels[7] = {
        "ACHIEVEMENT SOUND:", "ACHIEVEMENT SOUND:", "ACHIEVEMENT SOUND:",
        "ERFOLG-TON:", "SON NOTIFICATION:", "SUONO NOTIFICA:", "SONIDO LOGRO:"
    };
    DrawText(24.0f, 149.0f, 1.0f, sndLabels[lang], C2D_Color32(255, 255, 255, 255));
    bool snd = Port_RA_GetNotificationSound();
    DrawText(170.0f, 149.0f, 1.0f, snd ? ((lang == 6) ? "ACTIVADO" : ((lang == 3) ? "EIN" : ((lang == 4) ? "ACTIVE" : ((lang == 5) ? "ATTIVO" : "ON"))))
                                       : ((lang == 6) ? "DESACTIVADO" : ((lang == 3) ? "AUS" : ((lang == 4) ? "DESACTIVE" : ((lang == 5) ? "DISATTIVO" : "OFF")))),
             snd ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(255, 100, 100, 255));

    /* Close button */
    const char* closeRALabels[7] = {
        "CLOSE", "CLOSE", "CLOSE",
        "SCHLIESSEN", "FERMER", "CHIUDI", "CERRAR"
    };
    DrawButton(116.0f, 206.0f, 88.0f, 22.0f, closeRALabels[lang],
               C2D_Color32(255, 255, 255, 255), BTN_BLUE_BODY, BTN_BLUE_BORDER);
}

/* "What's new" viewer: plain wrapped text, "== vX.Y.Z ==" lines drawn as
 * gold headers, the rest as the list the release author wrote. */
static void RenderNotesModal(int lang) {
    const bool es = (lang == 6);
    const float maxScroll = NotesMaxScroll();

    C2D_DrawRectSolid(10.0f, 26.0f, 0.85f, 300.0f, 206.0f, C2D_Color32(10, 14, 24, 250));
    C2D_DrawRectSolid(10.0f, 26.0f, 0.84f, 300.0f, 206.0f, C2D_Color32(40, 70, 120, 255));
    DrawText(20.0f, 32.0f, 1.0f, es ? "NOVEDADES" : "WHAT'S NEW", C2D_Color32(255, 215, 0, 255));
    DrawText(170.0f, 32.0f, 1.0f, Port_Updater_GetRemoteTag(), C2D_Color32(140, 160, 190, 255));

    if (sNotesScrollY > maxScroll) sNotesScrollY = maxScroll;
    for (int i = 0; i < sNotesLineCount; ++i) {
        float y = NOTES_VIEW_Y0 - sNotesScrollY + (float)i * NOTES_LINE_H;
        const char* t = sNotesLines[i];
        if (y + NOTES_LINE_H <= NOTES_VIEW_Y0 || y >= NOTES_VIEW_Y1 || !t[0]) continue;
        if (t[0] == '=' && t[1] == '=') {
            char head[40];
            snprintf(head, sizeof(head), "%s", t + 3);
            size_t n = strlen(head);
            if (n >= 3) head[n - 3] = '\0'; /* trailing " ==" */
            DrawTextClipped(18.0f, y, 1.0f, head, C2D_Color32(255, 215, 0, 255), NOTES_VIEW_Y0, NOTES_VIEW_Y1);
        } else {
            DrawTextClipped(18.0f, y, 1.0f, t, C2D_Color32(220, 235, 255, 255), NOTES_VIEW_Y0, NOTES_VIEW_Y1);
        }
    }
    if (sNotesLineCount == 0) {
        DrawText(18.0f, NOTES_VIEW_Y0, 1.0f, es ? "AUN SIN DATOS. BUSCA ACTUALIZACION." : "NOTHING YET. CHECK NOW FIRST.", C2D_Color32(140, 160, 190, 255));
    }

    if (maxScroll > 0.0f) {
        float trackH = NOTES_VIEW_Y1 - NOTES_VIEW_Y0;
        float thumbH = 30.0f;
        float thumbY = NOTES_VIEW_Y0 + (sNotesScrollY / maxScroll) * (trackH - thumbH);
        C2D_DrawRectSolid(306.0f, NOTES_VIEW_Y0, 0.92f, 2.0f, trackH, C2D_Color32(255, 35, 55, 255));
        C2D_DrawRectSolid(306.0f, thumbY, 0.94f, 2.0f, thumbH, C2D_Color32(80, 160, 240, 255));
    }

    DrawButton(116.0f, 206.0f, 88.0f, 22.0f, es ? "CERRAR" : "CLOSE",
               C2D_Color32(255, 255, 255, 255), BTN_BLUE_BODY, BTN_BLUE_BORDER);
}

/* Self-update modal: auto-update + channel toggles, a status line and one
 * context-sensitive action button (check / install / restart). */
static void RenderUpdateModal(int lang) {
    const bool es = (lang == 6);
    const UpdaterState us = Port_Updater_GetState();
    const uint32_t rowBody = C2D_Color32(16, 24, 40, 255);
    const uint32_t rowBorder = C2D_Color32(50, 80, 130, 255);
    const uint32_t white = C2D_Color32(255, 255, 255, 255);
    const uint32_t green = C2D_Color32(80, 255, 120, 255);
    const uint32_t red = C2D_Color32(255, 100, 100, 255);
    char line[64];

    C2D_DrawRectSolid(10.0f, 26.0f, 0.85f, 300.0f, 206.0f, C2D_Color32(10, 14, 24, 250));
    C2D_DrawRectSolid(10.0f, 26.0f, 0.84f, 300.0f, 206.0f, C2D_Color32(40, 70, 120, 255));
    DrawText(20.0f, 32.0f, 1.0f, es ? "ACTUALIZACIONES" : "UPDATES", C2D_Color32(255, 215, 0, 255));
    DrawText(170.0f, 32.0f, 1.0f, MZM_PORT_VERSION_LABEL, C2D_Color32(140, 160, 190, 255));

    DrawButtonBox(16.0f, 56.0f, 288.0f, 24.0f, rowBody, rowBorder);
    DrawText(24.0f, 65.0f, 1.0f, es ? "AUTOACTUALIZAR:" : "AUTO UPDATE:", white);
    bool autoUp = Port_Updater_GetAuto();
    DrawText(170.0f, 65.0f, 1.0f, autoUp ? (es ? "ACTIVADO" : "ENABLED") : (es ? "DESACTIVADO" : "DISABLED"),
             autoUp ? green : red);

    DrawButtonBox(16.0f, 84.0f, 288.0f, 24.0f, rowBody, rowBorder);
    DrawText(24.0f, 93.0f, 1.0f, es ? "CANAL:" : "CHANNEL:", white);
    DrawText(170.0f, 93.0f, 1.0f,
             Port_Updater_GetBeta() ? (es ? "RELEASES + BETAS" : "RELEASES + BETAS") : (es ? "SOLO RELEASES" : "RELEASES ONLY"),
             C2D_Color32(120, 200, 255, 255));

    /* Status (not tappable) */
    DrawButtonBox(16.0f, 112.0f, 288.0f, 24.0f, rowBody, rowBorder);
    uint32_t statusCol = C2D_Color32(140, 160, 190, 255);
    switch (us) {
        case UPDATER_CHECKING:    snprintf(line, sizeof(line), "%s", es ? "BUSCANDO..." : "CHECKING..."); statusCol = C2D_Color32(255, 220, 80, 255); break;
        case UPDATER_UP_TO_DATE:  snprintf(line, sizeof(line), "%s", es ? "YA ESTAS AL DIA" : "UP TO DATE"); statusCol = green; break;
        case UPDATER_AVAILABLE:   snprintf(line, sizeof(line), "%s %s", es ? "NUEVA:" : "NEW:", Port_Updater_GetRemoteTag()); statusCol = C2D_Color32(255, 220, 80, 255); break;
        case UPDATER_DOWNLOADING: snprintf(line, sizeof(line), "%s %d%%", es ? "INSTALANDO" : "INSTALLING", Port_Updater_GetProgress()); statusCol = C2D_Color32(255, 220, 80, 255); break;
        case UPDATER_INSTALLED:   snprintf(line, sizeof(line), "%s %s", es ? "INSTALADA" : "INSTALLED", Port_Updater_GetRemoteTag()); statusCol = green; break;
        case UPDATER_ERROR:       snprintf(line, sizeof(line), "%s", Port_Updater_GetMessage()); statusCol = red; break;
        default:                  snprintf(line, sizeof(line), "%s", es ? "SIN COMPROBAR" : "NOT CHECKED"); break;
    }
    DrawText(24.0f, 121.0f, 1.0f, line, statusCol);
    if (us == UPDATER_DOWNLOADING) {
        C2D_DrawRectSolid(16.0f, 132.0f, 0.9f, 288.0f * (float)Port_Updater_GetProgress() / 100.0f, 4.0f, green);
    }

    /* Action button */
    const char* actionLabel;
    uint32_t actionBody = BTN_BLUE_BODY, actionBorder = BTN_BLUE_BORDER;
    switch (us) {
        case UPDATER_AVAILABLE:   actionLabel = es ? "INSTALAR AHORA" : "INSTALL NOW"; break;
        case UPDATER_INSTALLED:   actionLabel = es ? "REINICIAR AHORA" : "RESTART NOW"; break;
        case UPDATER_CHECKING:
        case UPDATER_DOWNLOADING: actionLabel = "..."; actionBody = C2D_Color32(30, 34, 44, 255); actionBorder = C2D_Color32(70, 76, 90, 255); break;
        default:                  actionLabel = es ? "BUSCAR ACTUALIZACION" : "CHECK NOW"; break;
    }
    DrawButton(16.0f, 142.0f, 288.0f, 28.0f, actionLabel, white, actionBody, actionBorder);

    /* Always there: with an update waiting it lists what the update brings, and
     * otherwise the latest published changes (empty until a check has run). */
    DrawButton(16.0f, 176.0f, 288.0f, 24.0f, es ? "NOVEDADES" : "WHAT'S NEW",
               C2D_Color32(255, 225, 120, 255), C2D_Color32(46, 36, 12, 255), C2D_Color32(170, 130, 60, 255));

    DrawButton(116.0f, 206.0f, 88.0f, 22.0f, es ? "CERRAR" : "CLOSE", white, BTN_BLUE_BODY, BTN_BLUE_BORDER);
}

/* Modal prompt for the self-updater, drawn over whichever tab is active. */
static void RenderUpdatePrompt(int lang) {
    const UpdaterPrompt pr = Port_Updater_GetPrompt();
    const bool es = (lang == 6);
    const uint32_t white = C2D_Color32(255, 255, 255, 255);
    const uint32_t gold = C2D_Color32(255, 215, 0, 255);
    const char* title = "";
    const char* question = "";
    char line[64];

    if (pr == UPDATER_PROMPT_NONE || sShowNotesModal) return;

    char probe[2];
    const bool hasNotes = (pr == UPDATER_PROMPT_ASK_INSTALL && Port_Updater_CopyNotes(probe, sizeof(probe)) > 0);
    const float extra = hasNotes ? 28.0f : 0.0f;

    C2D_DrawRectSolid(0.0f, 0.0f, 0.97f, 320.0f, 240.0f, C2D_Color32(0, 0, 0, 170));
    C2D_DrawRectSolid(20.0f, 58.0f, 0.98f, 280.0f, 122.0f + extra, C2D_Color32(40, 70, 120, 255));
    C2D_DrawRectSolid(22.0f, 60.0f, 0.99f, 276.0f, 118.0f + extra, C2D_Color32(10, 14, 24, 255));

    switch (pr) {
        case UPDATER_PROMPT_ASK_INSTALL:
            title = es ? "ACTUALIZACION DISPONIBLE" : "UPDATE AVAILABLE";
            question = es ? "INSTALAR AHORA?" : "INSTALL NOW?";
            snprintf(line, sizeof(line), "%s -> %s", MZM_PORT_VERSION_LABEL, Port_Updater_GetRemoteTag());
            break;
        case UPDATER_PROMPT_PROGRESS:
            title = es ? "ACTUALIZANDO" : "UPDATING";
            snprintf(line, sizeof(line), "%d%%", Port_Updater_GetProgress());
            break;
        case UPDATER_PROMPT_ASK_RESTART:
            title = es ? "ACTUALIZACION INSTALADA" : "UPDATE INSTALLED";
            question = es ? "REINICIAR AHORA?" : "RESTART NOW?";
            snprintf(line, sizeof(line), "%s", Port_Updater_GetRemoteTag());
            break;
        default:
            title = es ? "ERROR DE ACTUALIZACION" : "UPDATE FAILED";
            snprintf(line, sizeof(line), "%s", Port_Updater_GetMessage());
            break;
    }
    DrawTextCentered(160.0f, 70.0f, 1.0f, title, gold);
    DrawTextCentered(160.0f, 94.0f, 1.0f, line, C2D_Color32(150, 200, 255, 255));
    if (question[0]) DrawTextCentered(160.0f, 114.0f, 1.0f, question, white);
    if (pr == UPDATER_PROMPT_ERROR && Port_Updater_KeptCia()) {
        DrawTextCentered(160.0f, 112.0f, 1.0f, es ? "CIA EN SD: mzm-update.cia" : "CIA KEPT: sdmc:/mzm-update.cia",
                         C2D_Color32(255, 220, 80, 255));
    }

    if (pr == UPDATER_PROMPT_PROGRESS) {
        C2D_DrawRectSolid(40.0f, 132.0f, 0.99f, 240.0f, 8.0f, C2D_Color32(30, 40, 60, 255));
        C2D_DrawRectSolid(40.0f, 132.0f, 1.0f, 240.0f * (float)Port_Updater_GetProgress() / 100.0f, 8.0f,
                          C2D_Color32(80, 255, 120, 255));
    } else if (pr == UPDATER_PROMPT_ERROR) {
        DrawButton(116.0f, (float)UPD_BTN_Y0, 88.0f, 28.0f, es ? "ACEPTAR" : "OK", white, BTN_BLUE_BODY, BTN_BLUE_BORDER);
    } else {
        DrawButton((float)UPD_YES_X0, (float)UPD_BTN_Y0, (float)(UPD_YES_X1 - UPD_YES_X0), 28.0f,
                   es ? "SI" : "YES", white, C2D_Color32(16, 60, 32, 255), C2D_Color32(55, 150, 95, 255));
        DrawButton((float)UPD_NO_X0, (float)UPD_BTN_Y0, (float)(UPD_NO_X1 - UPD_NO_X0), 28.0f,
                   "NO", white, C2D_Color32(64, 22, 22, 255), C2D_Color32(180, 60, 60, 255));
        if (hasNotes) {
            DrawButton((float)UPD_YES_X0, (float)UPD_NOTES_Y0, (float)(UPD_NO_X1 - UPD_YES_X0),
                       (float)(UPD_NOTES_Y1 - UPD_NOTES_Y0), es ? "NOVEDADES" : "WHAT'S NEW",
                       C2D_Color32(255, 225, 120, 255), C2D_Color32(46, 36, 12, 255), C2D_Color32(170, 130, 60, 255));
        }
    }
}

/* Action picker popup for remapping */
static void RenderRemapSelectModal(int lang) {
    if (sRemapSelectButtonIdx < 0 || sRemapSelectButtonIdx >= 10) return;

    /* Dark semi-transparent background overlay */
    C2D_DrawRectSolid(12.0f, 28.0f, 0.96f, 296.0f, 204.0f, C2D_Color32(8, 12, 20, 252));
    C2D_DrawRectSolid(12.0f, 28.0f, 0.95f, 296.0f, 204.0f, C2D_Color32(40, 80, 140, 255));

    static const char* const btnNames[7][10] = {
        { "BUTTON A", "BUTTON B", "BUTTON X", "BUTTON Y", "BUTTON L", "BUTTON R", "BUTTON ZL", "BUTTON ZR", "START", "SELECT" },
        { "BUTTON A", "BUTTON B", "BUTTON X", "BUTTON Y", "BUTTON L", "BUTTON R", "BUTTON ZL", "BUTTON ZR", "START", "SELECT" },
        { "BUTTON A", "BUTTON B", "BUTTON X", "BUTTON Y", "BUTTON L", "BUTTON R", "BUTTON ZL", "BUTTON ZR", "START", "SELECT" },
        { "TASTE A", "TASTE B", "TASTE X", "TASTE Y", "TASTE L", "TASTE R", "TASTE ZL", "TASTE ZR", "START", "SELECT" },
        { "BOUTON A", "BOUTON B", "BOUTON X", "BOUTON Y", "BOUTON L", "BOUTON R", "BOUTON ZL", "BOUTON ZR", "START", "SELECT" },
        { "PULSANTE A", "PULSANTE B", "PULSANTE X", "PULSANTE Y", "PULSANTE L", "PULSANTE R", "PULSANTE ZL", "PULSANTE ZR", "START", "SELECT" },
        { "BOTON A", "BOTON B", "BOTON X", "BOTON Y", "BOTON L", "BOTON R", "BOTON ZL", "BOTON ZR", "START", "SELECT" }
    };

    char header[64];
    const char* selectActionTitles[7] = {
        "SELECT ACTION (%s)", "SELECT ACTION (%s)", "SELECT ACTION (%s)",
        "AKTION WAEHLEN (%s)", "CHOISIR ACTION (%s)", "SELEZIONA AZIONE (%s)", "SELECCIONAR ACCION (%s)"
    };
    snprintf(header, sizeof(header), selectActionTitles[lang], btnNames[lang][sRemapSelectButtonIdx]);
    DrawTextCentered(160.0f, 32.0f, 1.0f, header, C2D_Color32(255, 215, 0, 255));

    int currentAct = Port_Config_GetButtonMapping(sRemapSelectButtonIdx);

    extern bool Port_RA_IsHardcore(void);
    bool hcActive = Port_RA_IsHardcore();

    /* 9 actions arranged in 2 columns: 5 in col 0 (left: 0..4), 4 in col 1 (right: 5..8) + CANCEL */
    for (int act = 0; act < 9; ++act) {
        int col = act / 5;
        int row = act % 5;
        float bx = (col == 0) ? 18.0f : 162.0f;
        float by = 48.0f + (float)row * 28.0f;
        float bw = 140.0f;
        float bh = 24.0f;

        /* Rapid fire (1) and quick morph (2) are input assists RA hardcore
         * forbids; show them greyed, same as elsewhere. */
        bool isDisabled = ((act == 1 || act == 2) && hcActive);
        bool isCur = (act == currentAct);
        uint32_t bgCol, bdrCol, textCol;

        if (isDisabled) {
            bgCol = C2D_Color32(16, 20, 28, 255);
            bdrCol = C2D_Color32(35, 42, 55, 255);
            textCol = C2D_Color32(80, 90, 110, 255);
        } else if (isCur) {
            bgCol = C2D_Color32(30, 80, 140, 255);
            bdrCol = C2D_Color32(80, 180, 255, 255);
            textCol = C2D_Color32(255, 255, 255, 255);
        } else {
            bgCol = C2D_Color32(20, 28, 44, 255);
            bdrCol = C2D_Color32(45, 58, 85, 255);
            textCol = C2D_Color32(180, 200, 225, 255);
        }

        C2D_DrawRectSolid(bx, by, 0.97f, bw, bh, bdrCol);
        C2D_DrawRectSolid(bx + 1.0f, by + 1.0f, 0.98f, bw - 2.0f, bh - 2.0f, bgCol);
        DrawTextCentered(bx + bw / 2.0f, by + 9.0f, 1.0f, Port_Config_GetActionName(act, lang), textCol);
    }

    /* Cancel button in col 1, row 4 */
    float cx = 162.0f;
    float cy = 48.0f + 4.0f * 28.0f;
    C2D_DrawRectSolid(cx, cy, 0.97f, 140.0f, 24.0f, C2D_Color32(100, 30, 30, 255));
    C2D_DrawRectSolid(cx + 1.0f, cy + 1.0f, 0.98f, 138.0f, 22.0f, C2D_Color32(60, 20, 20, 255));
    const char* cancelLabels[7] = {
        "CANCEL", "CANCEL", "CANCEL",
        "ABBRECHEN", "ANNULER", "ANNULLA", "CANCELAR"
    };
    DrawTextCentered(cx + 70.0f, cy + 9.0f, 1.0f, cancelLabels[lang], C2D_Color32(255, 140, 140, 255));
}

static void RenderRemapModal(int lang) {
    /* Background overlay */
    C2D_DrawRectSolid(10.0f, 26.0f, 0.85f, 300.0f, 206.0f, C2D_Color32(10, 14, 24, 250));
    C2D_DrawRectSolid(10.0f, 26.0f, 0.84f, 300.0f, 206.0f, C2D_Color32(40, 70, 120, 255));

    /* Title at top */
    const char* remapTitles[7] = {
        "BUTTON REMAPPING (TAP TO CHANGE)", "BUTTON REMAPPING (TAP TO CHANGE)", "BUTTON REMAPPING (TAP TO CHANGE)",
        "TASTENBELEGUNG (ZUM AENDERN TIPPEN)", "RECONFIGURATION TOUCHES (TOUCHER)", "CONFIGURAZIONE TASTI (TOCCA)", "REASIGNAR BOTONES (TOCA PARA CAMBIAR)"
    };
    DrawText(20.0f, 31.0f, 1.0f, remapTitles[lang], C2D_Color32(255, 215, 0, 255));

    /* Viewable / scrollable region: Y 44 to 202 (158px) */
    const float viewY0 = 44.0f;
    const float viewY1 = 202.0f;
    const float maxScroll = 180.0f; /* 13 items * 26px = 338px; 338 - 158 = 180px */

    /* 10 remappable buttons + 1 C-Stick row = 11 items, each 26px tall */
    static const char* const btnRowNames[7][10] = {
        { "BUTTON A:", "BUTTON B:", "BUTTON X:", "BUTTON Y:", "BUTTON L:", "BUTTON R:", "BUTTON ZL:", "BUTTON ZR:", "START:", "SELECT:" },
        { "BUTTON A:", "BUTTON B:", "BUTTON X:", "BUTTON Y:", "BUTTON L:", "BUTTON R:", "BUTTON ZL:", "BUTTON ZR:", "START:", "SELECT:" },
        { "BUTTON A:", "BUTTON B:", "BUTTON X:", "BUTTON Y:", "BUTTON L:", "BUTTON R:", "BUTTON ZL:", "BUTTON ZR:", "START:", "SELECT:" },
        { "TASTE A:", "TASTE B:", "TASTE X:", "TASTE Y:", "TASTE L:", "TASTE R:", "TASTE ZL:", "TASTE ZR:", "START:", "SELECT:" },
        { "BOUTON A:", "BOUTON B:", "BOUTON X:", "BOUTON Y:", "BOUTON L:", "BOUTON R:", "BOUTON ZL:", "BOUTON ZR:", "START:", "SELECT:" },
        { "PULSANTE A:", "PULSANTE B:", "PULSANTE X:", "PULSANTE Y:", "PULSANTE L:", "PULSANTE R:", "PULSANTE ZL:", "PULSANTE ZR:", "START:", "SELECT:" },
        { "BOTON A:", "BOTON B:", "BOTON X:", "BOTON Y:", "BOTON L:", "BOTON R:", "BOTON ZL:", "BOTON ZR:", "START:", "SELECT:" }
    };

    float contentY = viewY0 - sRemapScrollY;
    extern bool Port_RA_IsHardcore(void);
    bool hcActive = Port_RA_IsHardcore();

    /* Draw 10 button rows with strict clipping */
    for (int i = 0; i < 10; ++i) {
        float py = contentY + (float)i * 26.0f;
        if (py + 24.0f <= viewY0 || py >= viewY1) continue;

        float rY0 = (py < viewY0) ? viewY0 : py;
        float rY1 = (py + 24.0f > viewY1) ? viewY1 : (py + 24.0f);
        if (rY1 > rY0) {
            C2D_DrawRectSolid(16.0f, rY0, 0.88f, 284.0f, rY1 - rY0, C2D_Color32(24, 32, 50, 255));
            C2D_DrawRectSolid(16.0f, rY0, 0.89f, 284.0f, 1.0f, C2D_Color32(45, 60, 90, 255));
        }

        DrawTextClipped(24.0f, py + 9.0f, 1.0f,
            btnRowNames[lang][i],
            C2D_Color32(255, 255, 255, 255), viewY0, viewY1);
        int act = Port_Config_GetButtonMapping(i);
        bool actDisabled = ((act == 1 || act == 2) && hcActive);
        uint32_t actCol = actDisabled ? C2D_Color32(110, 120, 140, 255) : C2D_Color32(255, 215, 0, 255);
        DrawTextClipped(150.0f, py + 9.0f, 1.0f,
            Port_Config_GetActionName(act, lang),
            actCol, viewY0, viewY1);
    }

    /* C-Stick mode row (item 11) */
    float cstickY = contentY + 10.0f * 26.0f;
    if (cstickY + 24.0f > viewY0 && cstickY < viewY1) {
        float rY0 = (cstickY < viewY0) ? viewY0 : cstickY;
        float rY1 = (cstickY + 24.0f > viewY1) ? viewY1 : (cstickY + 24.0f);
        if (rY1 > rY0) {
            C2D_DrawRectSolid(16.0f, rY0, 0.88f, 284.0f, rY1 - rY0, C2D_Color32(28, 40, 60, 255));
            C2D_DrawRectSolid(16.0f, rY0, 0.89f, 284.0f, 1.0f, C2D_Color32(50, 80, 120, 255));
        }

        DrawTextClipped(24.0f, cstickY + 9.0f, 1.0f,
            "C-STICK:",
            C2D_Color32(255, 255, 255, 255), viewY0, viewY1);
        int cmode = Port_Config_GetCstickMode();
        static const char* const cstickModeNames[7][4] = {
            { "OFF", "AIM ONLY", "MOVEMENT ONLY", "ALL" },
            { "OFF", "AIM ONLY", "MOVEMENT ONLY", "ALL" },
            { "OFF", "AIM ONLY", "MOVEMENT ONLY", "ALL" },
            { "AUS", "NUR ZIELEN", "NUR BEWEGUNG", "ALLES" },
            { "DESACTIVE", "VISER SEUL.", "DEPLAC. SEUL.", "TOUT" },
            { "DISATTIVO", "SOLO MIRA", "SOLO MOVIMENTO", "TUTTO" },
            { "DESACTIVADO", "SOLO APUNTAR", "SOLO MOVIMIENTO", "TODO" }
        };
        DrawTextClipped(150.0f, cstickY + 9.0f, 1.0f,
            cstickModeNames[lang][cmode],
            C2D_Color32(255, 215, 0, 255), viewY0, viewY1);
    }

    /* Items 12 and 13: AUTO BOMB JUMP (port_auto_bomb.c) and AUTO SPACE JUMP
     * (port_auto_space_jump.c). Off while RetroAchievements hardcore is on, like the other assists. */
    {
        extern bool Port_AutoBomb_Enabled(void);
        extern bool Port_AutoSpaceJump_Enabled(void);
        const bool values[2] = { Port_AutoBomb_Enabled(), Port_AutoSpaceJump_Enabled() };
        const char* const names[2][2] = { { "AUTO BOMB JUMP:", "AUTOSALTO BOMBAS:" },
                                          { "AUTO SPACE JUMP:", "AUTO SALTO ESPACIAL:" } };
        for (int i = 0; i < 2; ++i) {
            const float rowY = contentY + (float)(11 + i) * 26.0f;
            if (rowY + 24.0f <= viewY0 || rowY >= viewY1) continue;
            float rY0 = (rowY < viewY0) ? viewY0 : rowY;
            float rY1 = (rowY + 24.0f > viewY1) ? viewY1 : (rowY + 24.0f);
            if (rY1 > rY0) {
                C2D_DrawRectSolid(16.0f, rY0, 0.88f, 284.0f, rY1 - rY0, C2D_Color32(24, 32, 50, 255));
                C2D_DrawRectSolid(16.0f, rY0, 0.89f, 284.0f, 1.0f, C2D_Color32(45, 60, 90, 255));
            }
            DrawTextClipped(24.0f, rowY + 9.0f, 1.0f, names[i][lang == 6 ? 1 : 0],
                            C2D_Color32(255, 255, 255, 255), viewY0, viewY1);
            const bool on = values[i];
            const char* txt = hcActive ? ((lang == 6) ? "NO EN HARDCORE" : "NOT IN HARDCORE")
                                       : (on ? ((lang == 6) ? "ACTIVADO" : "ON") : ((lang == 6) ? "DESACT." : "OFF"));
            DrawTextClipped(150.0f, rowY + 9.0f, 1.0f, txt,
                            hcActive ? C2D_Color32(110, 120, 140, 255)
                                     : (on ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(255, 100, 100, 255)),
                            viewY0, viewY1);
        }
    }

    /* Scroll indicator on right edge */
    if (maxScroll > 0.0f) {
        float trackH = viewY1 - viewY0;
        float thumbH = 30.0f;
        float thumbY = viewY0 + (sRemapScrollY / maxScroll) * (trackH - thumbH);
        C2D_DrawRectSolid(304.0f, viewY0, 0.91f, 3.0f, trackH, C2D_Color32(20, 28, 44, 255));
        C2D_DrawRectSolid(304.0f, thumbY, 0.93f, 3.0f, thumbH, C2D_Color32(80, 160, 240, 255));
    }

    /* Bottom buttons (Fixed at bottom): RESTABLECER (Left) + CERRAR (Right) */
    /* Reset Defaults Button (X: 16 to 156, W: 140) */
    DrawButton(16.0f, 206.0f, 140.0f, 22.0f, (lang == 6) ? "RESTABLECER" : "RESET DEFAULTS",
               C2D_Color32(255, 170, 170, 255), C2D_Color32(90, 35, 35, 255), C2D_Color32(190, 80, 80, 255));

    /* Close button (X: 164 to 304, W: 140) */
    DrawButton(164.0f, 206.0f, 140.0f, 22.0f, (lang == 6) ? "CERRAR" : "CLOSE",
               C2D_Color32(255, 255, 255, 255), BTN_BLUE_BODY, BTN_BLUE_BORDER);

    /* Render action picker popup if active */
    if (sRemapSelectButtonIdx >= 0) {
        RenderRemapSelectModal(lang);
    }
}

/* Render Map View */
static void RenderMapView(void) {
    int lang = GetLang();
    if (sFollowSamus) {
        sViewArea = gCurrentArea;
        if (gCurrentArea < 7) {
            CenterMapOnTile(gMinimapX, gMinimapY);
        }
    }

    /* Area Stats for current/viewed area */
    struct AreaItemStats areaStats;
    GetAreaStats(sViewArea, &areaStats);

    /* 1. Sub-header bar controls (Y: 26 to 45) */
    /* Area selector [ < ] AreaName [ > ] */
    C2D_DrawRectSolid(4.0f, 26.0f, 0.4f, 16.0f, 18.0f, C2D_Color32(30, 40, 60, 255));
    DrawTextCentered(12.0f, 31.0f, 1.0f, "<", C2D_Color32(100, 220, 255, 255));

    C2D_DrawRectSolid(22.0f, 26.0f, 0.4f, 70.0f, 18.0f, sFollowSamus ? C2D_Color32(16, 50, 95, 255) : C2D_Color32(26, 32, 48, 255));
    DrawTextCentered(57.0f, 31.0f, 1.0f, AreaName(sViewArea), sFollowSamus ? C2D_Color32(255, 255, 255, 255) : C2D_Color32(180, 205, 235, 255));

    C2D_DrawRectSolid(94.0f, 26.0f, 0.4f, 16.0f, 18.0f, C2D_Color32(30, 40, 60, 255));
    DrawTextCentered(102.0f, 31.0f, 1.0f, ">", C2D_Color32(100, 220, 255, 255));

    /* Area Items Badge (Shows e.g. "5/12"; totals hidden in RA hardcore mode or if hide spoilers enabled) */
    char itemBadge[24];
    bool hideSpoilers = Port_RA_IsHardcore() || Port_Config_GetHideSpoilers();
    if (hideSpoilers) {
        snprintf(itemBadge, sizeof(itemBadge), "%u", areaStats.totalObtained);
    } else {
        snprintf(itemBadge, sizeof(itemBadge), "%u/%u", areaStats.totalObtained, areaStats.totalItems);
    }
    C2D_DrawRectSolid(112.0f, 26.0f, 0.4f, 44.0f, 18.0f, C2D_Color32(20, 45, 35, 255));
    C2D_DrawRectSolid(113.0f, 27.0f, 0.45f, 42.0f, 16.0f, C2D_Color32(12, 28, 20, 255));
    DrawTextCentered(134.0f, 31.0f, 1.0f, itemBadge, C2D_Color32(120, 255, 160, 255));

    /* Zoom Toggle Button */
    const char* zoomLabels[] = { "1X", "2X", "3X" };
    C2D_DrawRectSolid(156.0f, 26.0f, 0.4f, 40.0f, 18.0f, C2D_Color32(24, 45, 75, 255));
    C2D_DrawRectSolid(157.0f, 27.0f, 0.45f, 38.0f, 16.0f, C2D_Color32(14, 25, 45, 255));
    DrawTextCentered(176.0f, 31.0f, 1.0f, zoomLabels[sZoomLevel], C2D_Color32(255, 215, 0, 255));

    /* Center on Samus Button */
    C2D_DrawRectSolid(198.0f, 26.0f, 0.4f, 52.0f, 18.0f, sFollowSamus ? C2D_Color32(20, 80, 140, 255) : C2D_Color32(26, 35, 55, 255));
    DrawTextCentered(224.0f, 31.0f, 1.0f, "SAMUS", sFollowSamus ? C2D_Color32(255, 255, 255, 255) : C2D_Color32(140, 160, 190, 255));

    /* Chozo Objective / Coords Badge */
    uint8_t targetArea = 0, targetX = 0, targetY = 0;
    bool hasTarget = GetActiveChozoTarget(&targetArea, &targetX, &targetY);
    const char* objLabel = (lang == 6) ? "! OBJETIVO" : ((lang == 3) ? "! ZIEL" : "! TARGET");
    if (hasTarget) {
        bool inViewArea = (targetArea == sViewArea);
        uint32_t targetBg = inViewArea
            ? (((sFrameCounter % 20) < 10) ? C2D_Color32(200, 40, 40, 255) : C2D_Color32(160, 120, 0, 255))
            : C2D_Color32(80, 60, 20, 255);
        C2D_DrawRectSolid(252.0f, 26.0f, 0.4f, 64.0f, 18.0f, targetBg);
        DrawTextCentered(284.0f, 31.0f, 1.0f, objLabel, C2D_Color32(255, 255, 255, 255));
    } else {
        C2D_DrawRectSolid(252.0f, 26.0f, 0.4f, 64.0f, 18.0f, C2D_Color32(20, 25, 38, 255));
        char pBuf[16];
        snprintf(pBuf, sizeof(pBuf), "%02u,%02u", gMinimapX, gMinimapY);
        DrawTextCentered(284.0f, 31.0f, 1.0f, pBuf, C2D_Color32(140, 160, 190, 255));
    }

    /* 2. Full-height Map Canvas (Y: 48 to 236, X: 4 to 316, W: 312, H: 188) */
    const float canvasX = 4.0f;
    const float canvasY = 48.0f;
    const float canvasW = 312.0f;
    const float canvasH = 188.0f;
    const float clipX0 = canvasX;
    const float clipY0 = canvasY;
    const float clipX1 = canvasX + canvasW;
    const float clipY1 = canvasY + canvasH;

    /* Bezel & Canvas background */
#ifdef PORT_DEBUG_TOOLS_ACTIVE
    /* Armed tap-to-warp: a warning border, since the next tap on this canvas
     * teleports instead of panning. */
    if (sDebugMapWarpArmed) {
        C2D_DrawRectSolid(canvasX - 3.0f, canvasY - 3.0f, 0.28f, canvasW + 6.0f, canvasH + 6.0f,
                          ((sFrameCounter & 0x10) != 0) ? C2D_Color32(255, 200, 60, 255)
                                                        : C2D_Color32(120, 90, 20, 255));
    }
#endif
    C2D_DrawRectSolid(canvasX - 1.0f, canvasY - 1.0f, 0.3f, canvasW + 2.0f, canvasH + 2.0f, C2D_Color32(35, 50, 75, 255));
    C2D_DrawRectSolid(canvasX, canvasY, 0.35f, canvasW, canvasH, C2D_Color32(6, 10, 18, 255));

    /* Prepare map tiles */
    uint16_t* mapTiles = NULL;
    if (sViewArea == gCurrentArea) {
        mapTiles = gDecompressedMinimapVisitedTiles;
    } else {
        if (sCachedOtherArea != sViewArea) {
            PauseScreenGetMinimapData(sViewArea, sOtherAreaTiles);
            MinimapSetDownloadedTiles(sViewArea, sOtherAreaTiles);
            sCachedOtherArea = sViewArea;
        }
        mapTiles = sOtherAreaTiles;
    }

    float tileSize = GetTileSizeForZoom(sZoomLevel);
    float startDrawX = canvasX;
    float startDrawY = canvasY;

    if (sZoomLevel == 0) {
        /* Overview mode: center the 32x32 map in the large canvas */
        float totalMapW = 32.0f * tileSize;
        float totalMapH = 32.0f * tileSize;
        startDrawX = canvasX + (canvasW - totalMapW) / 2.0f;
        startDrawY = canvasY + (canvasH - totalMapH) / 2.0f;
    } else {
        startDrawX = canvasX - sScrollX;
        startDrawY = canvasY - sScrollY;
    }

    /* Draw All Authentic Map Tiles */
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            float tileDrawX = startDrawX + (float)x * tileSize;
            float tileDrawY = startDrawY + (float)y * tileSize;

            if (tileDrawX + tileSize < clipX0 || tileDrawX >= clipX1 ||
                tileDrawY + tileSize < clipY0 || tileDrawY >= clipY1) {
                continue;
            }

            uint16_t tileData = mapTiles[x + y * 32];
            if (tileData != 0 && tileData != 0x140) {
                DrawAuthenticMapTile(tileDrawX, tileDrawY, tileSize, tileData, clipX0, clipY0, clipX1, clipY1, 0.5f);
            }
        }
    }

    /* Draw Chozo Target Indicator on the map */
    if (hasTarget && targetArea == sViewArea && targetX < 32 && targetY < 32) {
        float tx = startDrawX + (float)targetX * tileSize;
        float ty = startDrawY + (float)targetY * tileSize;

        if (tx + tileSize >= clipX0 && tx <= clipX1 &&
            ty + tileSize >= clipY0 && ty <= clipY1) {
            uint32_t targetColor = ((sFrameCounter % 20) < 10) ? C2D_Color32(255, 50, 50, 255) : C2D_Color32(255, 220, 0, 255);
            float cx0 = tx < clipX0 ? clipX0 : tx;
            float cx1 = (tx + tileSize) > clipX1 ? clipX1 : (tx + tileSize);
            float cy0 = ty < clipY0 ? clipY0 : ty;
            float cy1 = (ty + tileSize) > clipY1 ? clipY1 : (ty + tileSize);

            if (cx1 > cx0 && cy1 > cy0) {
                C2D_DrawRectSolid(cx0, ty + (tileSize / 2.0f) - 1.0f, 0.75f, cx1 - cx0, 2.0f, targetColor);
                C2D_DrawRectSolid(tx + (tileSize / 2.0f) - 1.0f, cy0, 0.75f, 2.0f, cy1 - cy0, targetColor);
            }
        }
    }

    /* Route arrows: when the objective is in another area, mark the elevator
     * to take out of the area being viewed, up or down, the way the retail
     * pause map does. Same pulse as the target crosshair. */
    if (hasTarget && targetArea != sViewArea) {
        uint8_t elevX = 0, elevY = 0;
        int dir = 0;
        if (PortMap_RouteArrow(sViewArea, targetArea, &elevX, &elevY, &dir) &&
            elevX < 32 && elevY < 32) {
            float ex = startDrawX + ((float)elevX + 0.5f) * tileSize;
            float ey = startDrawY + ((float)elevY + 0.5f) * tileSize;
            if (ex >= clipX0 && ex <= clipX1 && ey >= clipY0 && ey <= clipY1) {
                uint32_t col = ((sFrameCounter % 20) < 10) ? C2D_Color32(255, 50, 50, 255)
                                                           : C2D_Color32(255, 220, 0, 255);
                float s = tileSize * 0.55f;
                if (s < 4.0f) s = 4.0f;
                if (s > 9.0f) s = 9.0f;
                float apexY = ey + (dir > 0 ? s : -s);
                float baseY = ey - (dir > 0 ? s : -s) * 0.35f;
                /* stem */
                C2D_DrawRectSolid(ex - 1.0f, (baseY < apexY ? baseY : apexY), 0.78f,
                                  2.0f, (baseY < apexY ? apexY - baseY : baseY - apexY), col);
                /* head */
                C2D_DrawTriangle(ex - s * 0.8f, baseY, col,
                                 ex + s * 0.8f, baseY, col,
                                 ex,            apexY, col, 0.79f);
            }
        }
    }

    /* Chozo-statue flame markers: after Kraid / Ridley die, a Chozo statue
     * opens its mouth and retail's map lights THAT statue tile with a green
     * (Kraid) or purple (Ridley) flame -- and shows no crosshair or route.
     * sChozoStatueTargets entries TARGET_KRAID_FLAME / TARGET_RIDLEY_FLAME put
     * the opened statue at target{Area,X,Y} (the flame's endIcon is the
     * settled state there); the statue box is only the flame's spawn point in
     * the retail moving animation, which the static map does not replay. */
    if (p_sChozoStatueTargets && ChozoStatueHintCheckTargetIsActivated) {
        for (int i = 0; i < 16; ++i) {
            const struct ChozoStatueTargetView* t = &(*p_sChozoStatueTargets)[i];
            if (t->startIcon < 8 /* TARGET_OAM_GREEN_FLAME_SPAWNING */) continue;
            if (t->targetArea != sViewArea) continue;
            if (t->targetX >= 32 || t->targetY >= 32) continue;
            if (ChozoStatueHintCheckTargetIsActivated((uint8_t)i) <= 0) continue;

            float fx = startDrawX + ((float)t->targetX + 0.5f) * tileSize;
            float fy = startDrawY + ((float)t->targetY + 0.5f) * tileSize;
            if (fx < clipX0 || fx > clipX1 || fy < clipY0 || fy > clipY1) continue;

            bool purple = (t->startIcon >= 12 /* TARGET_OAM_PURPLE_FLAME_SPAWNING */);
            uint32_t core = purple ? C2D_Color32(200, 90, 255, 255) : C2D_Color32(80, 255, 120, 255);
            uint32_t tip  = purple ? C2D_Color32(255, 210, 255, 255) : C2D_Color32(220, 255, 220, 255);

            float flick = (float)((sFrameCounter >> 2) & 3) * 0.12f;       /* 0..0.36 */
            float h = tileSize * (0.95f + flick);
            if (h < 6.0f) h = 6.0f;
            if (h > 14.0f) h = 14.0f;
            float w = h * 0.62f;
            float baseY = fy + h * 0.42f;
            float lean = ((sFrameCounter >> 3) & 1) ? w * 0.12f : -w * 0.12f;

            /* outer flame */
            C2D_DrawTriangle(fx - w * 0.5f, baseY, core,
                             fx + w * 0.5f, baseY, core,
                             fx + lean,     fy - h * 0.58f, core, 0.80f);
            /* base fill so the bottom is not a sharp point */
            C2D_DrawRectSolid(fx - w * 0.34f, baseY - h * 0.16f, 0.80f, w * 0.68f, h * 0.18f, core);
            /* inner bright tongue */
            C2D_DrawTriangle(fx - w * 0.24f, baseY - h * 0.05f, tip,
                             fx + w * 0.24f, baseY - h * 0.05f, tip,
                             fx + lean * 1.5f, fy - h * 0.30f, tip, 0.81f);
        }
    }

    /* Draw Samus Player indicator (when viewing current area) */
    if (sViewArea == gCurrentArea && gMinimapX < 32 && gMinimapY < 32) {
        float px = startDrawX + (float)gMinimapX * tileSize;
        float py = startDrawY + (float)gMinimapY * tileSize;

        if (px + tileSize >= clipX0 && px <= clipX1 &&
            py + tileSize >= clipY0 && py <= clipY1) {
            if ((sFrameCounter % 40) < 26) {
                C2D_DrawRectSolid(px - 1.0f, py - 1.0f, 0.8f, tileSize + 2.0f, tileSize + 2.0f, C2D_Color32(255, 220, 0, 255));
                C2D_DrawRectSolid(px, py, 0.9f, tileSize, tileSize, C2D_Color32(255, 40, 20, 255));
                C2D_DrawRectSolid(px + (tileSize * 0.25f), py + (tileSize * 0.25f), 0.95f, tileSize * 0.5f, tileSize * 0.5f, C2D_Color32(255, 255, 255, 255));
            }
        }
    }

#ifdef PORT_DEBUG_TOOLS_ACTIVE
    /* Map completion badge, top-left of the canvas. */
    if (Port_BottomUI_DebugTabVisible()) {
        char pctBuf[16];
        snprintf(pctBuf, sizeof(pctBuf), "%d%%", PortPpuMzm_DebugAreaMapPercent((int)sViewArea));
        float bw = (float)(Utf8CharCount(pctBuf) * 6 + 8);
        C2D_DrawRectSolid(canvasX + 3.0f, canvasY + 3.0f, 0.9f, bw, 12.0f, C2D_Color32(10, 16, 26, 220));
        C2D_DrawRectSolid(canvasX + 3.0f, canvasY + 3.0f, 0.91f, bw, 1.0f, C2D_Color32(60, 90, 140, 255));
        DrawText(canvasX + 7.0f, canvasY + 5.0f, 1.0f, pctBuf, C2D_Color32(120, 220, 255, 255));
    }

    /* Persistent Tap-to-Warp toggle button on the map canvas. Sized to the
     * text ("WARP: OFF" == 8 chars * 6px) with a small margin. */
    const float warpBtnW = 58.0f;
    const float warpBtnH = 16.0f;
    const float warpBtnX = 312.0f - warpBtnW;
    const float warpBtnY = 232.0f - warpBtnH;
    uint32_t warpBtnBg = sDebugMapWarpArmed
        ? (((sFrameCounter & 0x10) != 0) ? C2D_Color32(190, 140, 15, 255) : C2D_Color32(140, 95, 10, 255))
        : C2D_Color32(16, 24, 40, 230);
    uint32_t warpBtnBorder = sDebugMapWarpArmed
        ? C2D_Color32(255, 220, 50, 255)
        : C2D_Color32(45, 65, 95, 255);
    uint32_t warpBtnTextCol = sDebugMapWarpArmed
        ? C2D_Color32(255, 255, 255, 255)
        : C2D_Color32(140, 160, 190, 255);

    C2D_DrawRectSolid(warpBtnX - 1.0f, warpBtnY - 1.0f, 0.94f, warpBtnW + 2.0f, warpBtnH + 2.0f, warpBtnBorder);
    C2D_DrawRectSolid(warpBtnX, warpBtnY, 0.95f, warpBtnW, warpBtnH, warpBtnBg);
    DrawTextCentered(warpBtnX + warpBtnW / 2.0f, warpBtnY + (warpBtnH - 8.0f) / 2.0f, 1.0f,
                     sDebugMapWarpArmed ? "WARP: ON" : "WARP: OFF", warpBtnTextCol);
#endif

    /* Per-area collectibles breakdown, opened by tapping the item badge in
     * the subheader (the row for this area is highlighted). */
    if (sShowCollectiblesModal) RenderCollectiblesModal(GetLang());
}

/* Render Status (Estado) View */
static void RenderStatusView(void) {
    int lang = GetLang();
    /* In RA hardcore mode or if hide spoilers enabled, unobtained equipment is hidden as "----" (no spoilers) */
    bool hcStatus = Port_RA_IsHardcore() || Port_Config_GetHideSpoilers();

    C2D_DrawRectSolid(4.0f, 26.0f, 0.4f, 312.0f, 210.0f, C2D_Color32(12, 16, 26, 255));
    C2D_DrawRectSolid(4.0f, 26.0f, 0.35f, 312.0f, 210.0f, C2D_Color32(35, 50, 75, 255));

    const char* titles[7] = {
        "SAMUS ARAN - STATUS & EQUIPMENT",
        "SAMUS ARAN - STATUS & EQUIPMENT",
        "SAMUS ARAN - STATUS & EQUIPMENT",
        "SAMUS ARAN - STATUS & AUSRUESTUNG",
        "SAMUS ARAN - STATUT & EQUIPEMENT",
        "SAMUS ARAN - STATO ED EQUIPAGGIAMENTO",
        "SAMUS ARAN - ESTADO Y EQUIPAMIENTO"
    };
    DrawText(12.0f, 30.0f, 1.0f, titles[lang], C2D_Color32(100, 220, 255, 255));

#ifdef PORT_DEBUG_TOOLS_ACTIVE
    if (Port_BottomUI_DebugTabVisible()) DrawStatusDebugButtons();
#endif

    /* 1. Header Resource Card (X: 8, Y: 44 to 72, W: 304, H: 28) */
    C2D_DrawRectSolid(8.0f, 44.0f, 0.45f, 304.0f, 30.0f, C2D_Color32(20, 26, 40, 255));

    /* Health — color shifts with life level (only during real gameplay) */
    DrawEnergyIcon(14.0f, 55.0f);
    char eBuf[32];
    snprintf(eBuf, sizeof(eBuf), "%02u/%02u", gEquipment.currentEnergy, gEquipment.maxEnergy);
    bool inRealGameplay = ((uint8_t)gMainGameMode == 4 /* GM_INGAME */);
    uint32_t energyCol = C2D_Color32(255, 215, 0, 255); /* normal gold */
    if (inRealGameplay && gEquipment.currentEnergy < 30)
        energyCol = C2D_Color32(255, 70, 50, 255);       /* critical red */
    else if (inRealGameplay && gEquipment.currentEnergy < 60)
        energyCol = C2D_Color32(255, 200, 50, 255);      /* warning yellow */
    DrawText(31.0f, 56.0f, 1.0f, eBuf, energyCol);
    DrawResourceBar(14.0f, 67.0f, 66.0f, C2D_Color32(255, 200, 60, 255),
                    gEquipment.currentEnergy, gEquipment.maxEnergy);

    /* Missiles — icon/number/bar only when owned; box highlights when selected.
     * The whole 86..160 cell is tappable to switch to normal missiles (same as
     * SELECT) -- see Port_BottomUI_HandleTouchDrag. */
    {
        bool normSelected = (gSamusWeaponInfo.missilesSelected == 0);
        if (gEquipment.maxMissiles > 0) {
            if (normSelected) {
                C2D_DrawRectSolid(86.0f, 50.0f, 0.46f, 74.0f, 22.0f, C2D_Color32(30, 50, 90, 255));
                C2D_DrawRectSolid(86.0f, 50.0f, 0.47f, 74.0f, 1.0f, C2D_Color32(80, 180, 255, 255));
                C2D_DrawRectSolid(86.0f, 71.0f, 0.47f, 74.0f, 1.0f, C2D_Color32(80, 180, 255, 255));
            }
            DrawMissileIcon(90.0f, 55.0f);
            char mBuf[32];
            snprintf(mBuf, sizeof(mBuf), "%03u/%03u", gEquipment.currentMissiles, gEquipment.maxMissiles);
            DrawText(107.0f, 56.0f, 1.0f, mBuf, normSelected ? C2D_Color32(255, 200, 200, 255) : C2D_Color32(255, 140, 140, 255));
            DrawResourceBar(90.0f, 67.0f, 66.0f, C2D_Color32(232, 84, 84, 255),
                            gEquipment.currentMissiles, gEquipment.maxMissiles);
        }
    }

    /* Super Missiles — same treatment; cell tap selects supers. */
    {
        bool superSelected = (gSamusWeaponInfo.missilesSelected != 0);
        if (gEquipment.maxSuperMissiles > 0) {
            if (superSelected) {
                C2D_DrawRectSolid(162.0f, 50.0f, 0.46f, 74.0f, 22.0f, C2D_Color32(20, 50, 30, 255));
                C2D_DrawRectSolid(162.0f, 50.0f, 0.47f, 74.0f, 1.0f, C2D_Color32(80, 255, 140, 255));
                C2D_DrawRectSolid(162.0f, 71.0f, 0.47f, 74.0f, 1.0f, C2D_Color32(80, 255, 140, 255));
            }
            DrawSuperMissileIcon(166.0f, 55.0f);
            char smBuf[32];
            snprintf(smBuf, sizeof(smBuf), "%02u/%02u", gEquipment.currentSuperMissiles, gEquipment.maxSuperMissiles);
            DrawText(183.0f, 56.0f, 1.0f, smBuf, superSelected ? C2D_Color32(160, 255, 200, 255) : C2D_Color32(100, 255, 140, 255));
            DrawResourceBar(166.0f, 67.0f, 66.0f, C2D_Color32(96, 224, 136, 255),
                            gEquipment.currentSuperMissiles, gEquipment.maxSuperMissiles);
        }
    }

    /* Power Bombs — icon/number/bar only when owned. */
    if (gEquipment.maxPowerBombs > 0) {
        DrawPowerBombIcon(238.0f, 55.0f);
        char pbBuf[32];
        snprintf(pbBuf, sizeof(pbBuf), "%02u/%02u", gEquipment.currentPowerBombs, gEquipment.maxPowerBombs);
        DrawText(254.0f, 56.0f, 1.0f, pbBuf, C2D_Color32(255, 225, 80, 255));
        DrawResourceBar(242.0f, 67.0f, 62.0f, C2D_Color32(255, 196, 72, 255),
                        gEquipment.currentPowerBombs, gEquipment.maxPowerBombs);
    }

    /* 2. Beams & Weapons Column (X: 8, W: 148, Y: 76 to 164) */
    C2D_DrawRectSolid(8.0f, 76.0f, 0.45f, 148.0f, 88.0f, C2D_Color32(18, 22, 34, 255));
    const char* beamColTitles[7] = {
        "BEAMS & BOMBS", "BEAMS & BOMBS", "BEAMS & BOMBS",
        "BEAMS & BOMBEN", "RAYONS & BOMBES", "RAGGI E BOMBE", "RAYOS Y BOMBAS"
    };
    DrawText(14.0f, 80.0f, 1.0f, beamColTitles[lang], C2D_Color32(255, 215, 0, 255));

    const char* unknownNames[7] = {
        "UNKNOWN ITEM", "UNKNOWN ITEM", "UNKNOWN ITEM", "UNBEKANNT", "OBJET INCONNU", "OGGETTO SCONOSCIUTO", "OBJETO DESCONOCIDO"
    };
    const char* pistolNames[7] = {
        "PARALYZER", "PARALYZER", "PARALYZER", "NOTFALLPISTOLE", "PISTOLET URGENCE", "PISTOLA EMERGENZA", "PISTOLA EMERGENCIA"
    };

    bool isSuitless = (gEquipment.suitType == 2 /* SUIT_SUITLESS */);
    bool isFullyPowered = (gEquipment.suitType == 1 /* SUIT_FULLY_POWERED */);

    struct {
        const char* name[7];
        uint8_t flag;
        bool isUnknownItem;
    } beams[] = {
        { { "LONG BEAM", "LONG BEAM", "LONG BEAM", "DISTANCE BEAM", "RAYON LONG", "RAGGIO LUNGO", "RAYO LARGO" }, 1 << 0, false },
        { { "ICE BEAM", "ICE BEAM", "ICE BEAM", "ICE BEAM", "RAYON GLACE", "RAGGIO GELO", "RAYO HIELO" }, 1 << 1, false },
        { { "WAVE BEAM", "WAVE BEAM", "WAVE BEAM", "WAVE BEAM", "RAYON ONDES", "RAGGIO ONDA", "RAYO ONDAS" }, 1 << 2, false },
        { { "PLASMA BEAM", "PLASMA BEAM", "PLASMA BEAM", "PLASMA BEAM", "RAYON PLASMA", "RAGGIO PLASMA", "RAYO PLASMA" }, 1 << 3, true },
        { { "CHARGE BEAM", "CHARGE BEAM", "CHARGE BEAM", "CHARGE BEAM", "RAYON CHARGE", "RAGGIO CARICA", "RAYO RECARGA" }, 1 << 4, false },
        { { "BOMBS", "BOMBS", "BOMBS", "BOMBEN", "BOMBES", "BOMBE", "BOMBAS" }, 1 << 7, false }
    };
    for (int i = 0; i < 6; ++i) {
        float py = 92.0f + (float)i * 11.0f;
        if (isSuitless) {
            if (i == 0) {
                DrawText(14.0f, py, 1.0f, pistolNames[lang], C2D_Color32(80, 255, 120, 255));
                DrawText(130.0f, py, 1.0f, "OK", C2D_Color32(80, 255, 120, 255));
            } else {
                bool gotSuitless = (gEquipment.suitMisc & beams[i].flag) != 0;
                DrawText(14.0f, py, 1.0f,
                         (hcStatus && !gotSuitless) ? "----" : beams[i].name[lang],
                         C2D_Color32(75, 85, 105, 255));
                DrawText(130.0f, py, 1.0f, "--", C2D_Color32(75, 85, 105, 255));
            }
            continue;
        }

        bool has = (gEquipment.beamBombs & beams[i].flag) != 0;
        bool active = (gEquipment.beamBombsActivation & beams[i].flag) != 0;
        bool unknown = beams[i].isUnknownItem && !isFullyPowered && has;

        const char* label = (unknown && hcStatus) ? unknownNames[lang] : beams[i].name[lang];
        if (hcStatus && !has) label = "----";
        const char* statusStr = "--";
        uint32_t col = C2D_Color32(75, 85, 105, 255);

        if (has) {
            if (unknown) {
                statusStr = "??";
                col = C2D_Color32(255, 180, 50, 255);
            } else if (active) {
                statusStr = "OK";
                col = C2D_Color32(80, 255, 120, 255);
            } else {
                statusStr = "OFF";
                col = C2D_Color32(220, 160, 60, 255);
            }
        }

        DrawText(14.0f, py, 1.0f, label, col);
        DrawText(130.0f, py, 1.0f, statusStr, col);
    }

    /* 3. Suits & Movement Column (X: 164, W: 148, Y: 76 to 164) */
    C2D_DrawRectSolid(164.0f, 76.0f, 0.45f, 148.0f, 88.0f, C2D_Color32(18, 22, 34, 255));
    const char* suitColTitles[7] = {
        "SUITS & MISC", "SUITS & MISC", "SUITS & MISC",
        "ANZUEGE & ITEMS", "COMBINAISONS", "TUTE E OGGETTI", "TRAJES Y EQUIPO"
    };
    DrawText(170.0f, 80.0f, 1.0f, suitColTitles[lang], C2D_Color32(255, 215, 0, 255));

    struct {
        const char* name[7];
        uint8_t flag;
        bool isUnknownItem;
    } suits[] = {
        { { "HIGH JUMP", "HIGH JUMP", "HIGH JUMP", "HOCHSPRUNG", "SUPER SAUT", "SALTO IN ALTO", "SUPERSALTO" }, 1 << 0, false },
        { { "SPEED BOOSTER", "SPEED BOOSTER", "SPEED BOOSTER", "SPEED BOOSTER", "ACCELERATEUR", "SUPERVELOCITA", "ACELERACION" }, 1 << 1, false },
        { { "SPACE JUMP", "SPACE JUMP", "SPACE JUMP", "SPACE JUMP", "SAUT SPATIAL", "SALTO SPAZIALE", "SALTO ESPACIAL" }, 1 << 2, true },
        { { "SCREW ATTACK", "SCREW ATTACK", "SCREW ATTACK", "SCREW ATTACK", "ATTAQUE VRILLE", "ATTACCO A VITE", "SALTO EN BARRENA" }, 1 << 3, false },
        { { "VARIA SUIT", "VARIA SUIT", "VARIA SUIT", "VARIA SUIT", "COSTUME VARIA", "TUTA VARIA", "TRAJE CLIMATICO" }, 1 << 4, false },
        { { "GRAVITY SUIT", "GRAVITY SUIT", "GRAVITY SUIT", "GRAVITY SUIT", "COSTUME GRAVITE", "TUTA GRAVITA", "TRAJE GRAVITATORIO" }, 1 << 5, true },
        { { "MORPH BALL", "MORPH BALL", "MORPH BALL", "MORPH BALL", "BOULE MORPHING", "MORFOSFERA", "MORFOSFERA" }, 1 << 6, false },
        { { "POWER GRIP", "POWER GRIP", "POWER GRIP", "POWER GRIP", "POIGNE DE FER", "PRESA FORZA", "AGARRE" }, 1 << 7, false }
    };
    for (int i = 0; i < 8; ++i) {
        float py = 91.0f + (float)i * 9.0f;
        uint8_t flag = suits[i].flag;

        if (isSuitless) {
            bool hasSuitlessItem = (flag == (1 << 6) || flag == (1 << 7)) && ((gEquipment.suitMisc & flag) != 0);
            if (hasSuitlessItem) {
                DrawText(170.0f, py, 1.0f, suits[i].name[lang], C2D_Color32(80, 255, 120, 255));
                DrawText(286.0f, py, 1.0f, "OK", C2D_Color32(80, 255, 120, 255));
            } else {
                DrawText(170.0f, py, 1.0f, suits[i].name[lang], C2D_Color32(75, 85, 105, 255));
                DrawText(286.0f, py, 1.0f, "--", C2D_Color32(75, 85, 105, 255));
            }
            continue;
        }

        bool has = (gEquipment.suitMisc & flag) != 0;
        bool active = (gEquipment.suitMiscActivation & flag) != 0;
        bool unknown = suits[i].isUnknownItem && !isFullyPowered && has;

        const char* label = (unknown && hcStatus) ? unknownNames[lang] : suits[i].name[lang];
        if (hcStatus && !has) label = "----";
        const char* statusStr = "--";
        uint32_t col = C2D_Color32(75, 85, 105, 255);

        if (has) {
            if (unknown) {
                statusStr = "??";
                col = C2D_Color32(255, 180, 50, 255);
            } else if (active) {
#ifdef PORT_DEBUG_TOOLS_ACTIVE
                if (Port_BottomUI_DebugTabVisible() && PortPpuMzm_DebugGetForceEffect(flag)) {
                    statusStr = "AUTO";
                    col = C2D_Color32(120, 220, 255, 255);
                } else
#endif
                {
                    statusStr = "OK";
                    col = C2D_Color32(80, 255, 120, 255);
                }
            } else {
                statusStr = "OFF";
                col = C2D_Color32(220, 160, 60, 255);
            }
        }

        DrawText(170.0f, py, 1.0f, label, col);
        DrawText(286.0f, py, 1.0f, statusStr, col);
    }

    /* 4. Area Map Downloads Card & Collectibles Button (Y: 168 to 234) */
    C2D_DrawRectSolid(8.0f, 168.0f, 0.45f, 304.0f, 66.0f, C2D_Color32(16, 20, 30, 255));

    const char* mapHeaderTitles[7] = {
        "DOWNLOADED MAPS:", "DOWNLOADED MAPS:", "DOWNLOADED MAPS:",
        "HERUNTERGELADENE KARTEN:", "CARTES TELECHARGEES:", "MAPPE SCARICATE:", "MAPAS DESCARGADOS:"
    };
    DrawText(14.0f, 172.0f, 1.0f, mapHeaderTitles[lang], C2D_Color32(100, 220, 255, 255));

    /* Button for opening Collectibles Detail Modal */
    struct AreaItemStats globalStats;
    GetGlobalItemStats(&globalStats);
    char globBtn[48];
    static const char* const itemsBtnLabels[7] = {
        "アイテム", "アイテム", "ITEMS",
        "OBJEKTE", "OBJETS", "OGGETTI", "OBJETOS"
    };
    if (hcStatus) {
        snprintf(globBtn, sizeof(globBtn), "%s: %u", itemsBtnLabels[lang],
                 globalStats.totalObtained);
    } else {
        unsigned pct = globalStats.totalItems > 0 ? (globalStats.totalObtained * 100 / globalStats.totalItems) : 0;
        snprintf(globBtn, sizeof(globBtn), "%s: %u/%u (%u%%)", itemsBtnLabels[lang],
                 globalStats.totalObtained, globalStats.totalItems, pct);
    }
    C2D_DrawRectSolid(182.0f, 170.0f, 0.5f, 126.0f, 14.0f, C2D_Color32(20, 50, 90, 255));
    C2D_DrawRectSolid(183.0f, 171.0f, 0.55f, 124.0f, 12.0f, C2D_Color32(12, 30, 60, 255));
    DrawTextCentered(245.0f, 173.0f, 1.0f, globBtn, C2D_Color32(255, 215, 0, 255));

    /* Box colour is driven by the per-area debug cycle state, not by % :
     *   not downloaded            -> grey
     *   really downloaded (save)  -> green   (cycle state 0 + real dl bit)
     *   forced download  (debug)  -> purple  (cycle state 1)
     *   forced 100%      (debug)  -> orange  (cycle state 2)
     * In state 0 the downloaded bit reflects the genuine save (ClearAreaMap
     * restores it from the backup), so it is safe to read here. */
    for (int row = 0; row < 2; ++row) {
        int i0 = row == 0 ? 0 : 4, i1 = row == 0 ? 4 : 7;
        float by = row == 0 ? 188.0f : 210.0f;
        float bw = row == 0 ? 68.0f : 92.0f;
        float step = row == 0 ? 73.0f : 98.0f;
        for (int i = i0; i < i1; ++i) {
            bool dl = (gEquipment.downloadedMapStatus & (1 << i)) != 0;
            int mst = 0;
#ifdef PORT_DEBUG_TOOLS_ACTIVE
            if (Port_BottomUI_DebugTabVisible()) mst = sMapDebugState[i];
#endif
            float bx = 14.0f + (float)(i - i0) * step;
            uint32_t boxBg, boxBorder;
            bool lit;
            if (mst == 2) {                 /* forced 100% -> orange */
                boxBg = C2D_Color32(70, 45, 15, 255);
                boxBorder = C2D_Color32(255, 170, 60, 255); lit = true;
            } else if (mst == 1) {          /* forced download -> purple */
                boxBg = C2D_Color32(45, 30, 70, 255);
                boxBorder = C2D_Color32(170, 110, 240, 255); lit = true;
            } else if (dl) {                /* genuinely downloaded -> green */
                boxBg = C2D_Color32(18, 62, 32, 255);
                boxBorder = C2D_Color32(70, 220, 110, 255); lit = true;
            } else {                        /* not downloaded -> grey */
                boxBg = C2D_Color32(22, 26, 38, 255);
                boxBorder = C2D_Color32(45, 52, 70, 255); lit = false;
            }
            uint32_t textCol = lit ? C2D_Color32(255, 255, 255, 255)
                                   : C2D_Color32(90, 100, 120, 255);
            C2D_DrawRectSolid(bx, by, 0.5f, bw, 18.0f, boxBorder);
            C2D_DrawRectSolid(bx + 1.0f, by + 1.0f, 0.55f, bw - 2.0f, 16.0f, boxBg);
            DrawTextCentered(bx + bw / 2.0f, by + 5.0f, 1.0f, AreaName(i), textCol);
        }
    }

    if (sShowCollectiblesModal) {
        RenderCollectiblesModal(lang);
    }
}

/* Render Options View — 3 modal buttons + restart, no scrolling needed */
static void RenderOptionsView(void) {
    int lang = GetLang();

    const float viewX0 = 8.0f;
    const float viewY0 = 28.0f;
    const float viewW  = 304.0f;
    const float viewH  = 204.0f;
    const float viewY1 = viewY0 + viewH;

    /* Background panel */
    C2D_DrawRectSolid(viewX0, viewY0, 0.4f, viewW, viewH, C2D_Color32(14, 18, 28, 255));
    C2D_DrawRectSolid(viewX0, viewY0, 0.38f, viewW, viewH, C2D_Color32(35, 45, 70, 255));

    /* Button 1: Display settings (Y: 48 to 76, H: 28) */
    bool p1 = DrawButtonBox(16.0f, 48.0f, 288.0f, 28.0f,
                            C2D_Color32(16, 40, 74, 255), C2D_Color32(60, 110, 180, 255));
    static const char* const displayBtnTitles[7] = {
        "DISPLAY", "DISPLAY", "DISPLAY",
        "BILDSCHIRM", "AFFICHAGE", "SCHERMO", "PANTALLA"
    };
    static const char* const displayBtnSubs[7] = {
        "LANGUAGE, ASPECT, STYLE, FPS", "LANGUAGE, ASPECT, STYLE, FPS", "LANGUAGE, ASPECT, STYLE, FPS",
        "SPRACHE, BILD, STIL, FPS", "LANGUE, FORMAT, STYLE, FPS", "LINGUA, FORMATO, STILE, FPS", "IDIOMA, ASPECTO, ESTILO, FPS"
    };
    DrawTextCentered(160.0f, 53.0f + (p1 ? 1.0f : 0.0f), 1.0f, displayBtnTitles[lang], C2D_Color32(120, 210, 255, 255));
    DrawTextCentered(160.0f, 64.0f + (p1 ? 1.0f : 0.0f), 1.0f, displayBtnSubs[lang], C2D_Color32(150, 170, 200, 255));

    /* Controls remapping (Y: 82 to 114, H: 32) */
    bool p4 = DrawButtonBox(16.0f, 82.0f, 288.0f, 32.0f,
                            C2D_Color32(46, 32, 12, 255), C2D_Color32(170, 130, 60, 255));
    static const char* const ctrlTitles[7] = {
        "CONTROLS", "CONTROLS", "CONTROLS",
        "STEUERUNG", "COMMANDES", "CONTROLLI", "CONTROLES"
    };
    static const char* const ctrlSubs[7] = {
        "REMAPPABLE BUTTONS & C-STICK", "REMAPPABLE BUTTONS & C-STICK", "REMAPPABLE BUTTONS & C-STICK",
        "TASTEN & C-STICK BELEGEN", "TOUCHES ET C-STICK", "RIMAPPA TASTI E C-STICK", "MAPEAR BOTONES Y C-STICK"
    };
    DrawTextCentered(160.0f, 89.0f + (p4 ? 1.0f : 0.0f), 1.0f, ctrlTitles[lang], C2D_Color32(255, 215, 90, 255));
    DrawTextCentered(160.0f, 100.0f + (p4 ? 1.0f : 0.0f), 1.0f, ctrlSubs[lang], C2D_Color32(190, 170, 120, 255));

    /* Restart button (Y: 122 to 150, H: 28) */
    static const char* const restartBtnTitles[7] = {
        "RESTART GAME", "RESTART GAME", "RESTART GAME",
        "SPIEL NEUSTARTEN", "RECOMMENCER PARTIE", "RIAVVIA PARTITA", "REINICIAR PARTIDA"
    };
    DrawButton(16.0f, 122.0f, 140.0f, 28.0f, restartBtnTitles[lang],
               C2D_Color32(255, 150, 150, 255), C2D_Color32(64, 22, 22, 255), C2D_Color32(180, 60, 60, 255));

    /* Updates button, right half of the restart row */
    DrawButton(164.0f, 122.0f, 140.0f, 28.0f, (lang == 6) ? "ACTUALIZAR" : "UPDATES",
               C2D_Color32(150, 230, 255, 255), C2D_Color32(16, 44, 64, 255), C2D_Color32(60, 130, 180, 255));

    /* Footer */
    DrawTextCentered(160.0f, 212.0f, 1.0f, "METROID ZERO MISSION 3DS " MZM_PORT_VERSION_LABEL, C2D_Color32(90, 115, 145, 255));

    /* Render active modal on top */
    if (sShowDisplayModal) RenderDisplayModal(lang);
    else if (sShowNotesModal) RenderNotesModal(lang);
    else if (sShowUpdateModal) RenderUpdateModal(lang);
    else if (sShowRemapModal) RenderRemapModal(lang);
    else if (sShowConfirmModal) RenderConfirmModal(lang);
}

#ifdef PORT_DEBUG_TOOLS_ACTIVE
/* Geometry shared by the debug modals and their touch handlers. The modal
 * body is y 26..232 with a close button at 208, so anything drawn past ~204
 * lands under it -- which is exactly what a one-row-per-tool list did once
 * there were nine tools. Everything below is laid out as a two-per-line
 * grid instead, which fits every tool on screen without scrolling. */
#define DBGTOOL_COL_L_X   16
#define DBGTOOL_COL_R_X   164
#define DBGTOOL_COL_W     140
#define DBGTOOL_GRID_Y0   42   /* clears the modal title at y=32..39 */
#define DBGTOOL_GRID_PITCH 20
#define DBGTOOL_CELL_H    19
/* Cells 9..11 (RENDERER, PERFIL 3DS, PROFUNDIDAD) only exist when the GPU
 * tile renderer is compiled in -- a RENDERER=cpu build has nothing to switch
 * to. */
#ifdef PORT_GPU_TILE_RENDERER
#define DBGTOOL_COUNT     14
#else
#define DBGTOOL_COUNT     11
#endif
/* Always the last cell: opens the SOUND TEST sub-modal. Just before it, the FTP SERVER. */
#define DBGTOOL_SOUNDTEST_CELL (DBGTOOL_COUNT - 1)
#define DBGTOOL_FTP_CELL (DBGTOOL_COUNT - 2)
/* DERIVED, never hand-written. It used to be a literal 6, which was right
 * for twelve tools and silently wrong for the thirteenth, which drew itself
 * on row 6 -- on top of the status line and the CLOSE button --
 * while DebugCellHit still stopped scanning at row 5, so the row was
 * visible, overlapping, and untouchable. Deriving it means adding a tool
 * moves the grid instead of falling off the end of it. */
#define DBGTOOL_GRID_ROWS ((DBGTOOL_COUNT + 1) / 2)
/* The grid has to clear the transient status line at y=204 (see
 * RenderDebugToolsModal) and the CLOSE button at y=214. Checked here so a
 * future tool that no longer fits fails the build rather than the touch. */
_Static_assert(DBGTOOL_GRID_Y0 + (DBGTOOL_GRID_ROWS - 1) * DBGTOOL_GRID_PITCH + DBGTOOL_CELL_H <= 202,
               "debug tools grid runs into the status line / CLOSE button");

#define DBGTOOL_CELL_Y(r) ((float)(DBGTOOL_GRID_Y0 + (r) * DBGTOOL_GRID_PITCH))
#define DBGTOOL_CELL_X(c) ((float)((c) == 0 ? DBGTOOL_COL_L_X : DBGTOOL_COL_R_X))

/* A cell is a label plus a small state/affordance line under it. `accent`
 * tints the state line -- green for an active toggle, yellow for something
 * that opens a submenu, muted blue for a plain action. */
static void DrawDebugCell(int index, const char* label, const char* state, uint32_t accent) {
    float x = DBGTOOL_CELL_X(index & 1);
    float y = DBGTOOL_CELL_Y(index >> 1);
    DrawButtonBox(x, y, (float)DBGTOOL_COL_W, (float)DBGTOOL_CELL_H,
                  C2D_Color32(24, 32, 50, 255), C2D_Color32(50, 80, 130, 255));
    DrawTextMaxWClipped(x + 6.0f, y + 2.0f, 1.0f, label, C2D_Color32(255, 255, 255, 255),
                        0.0f, 240.0f, (float)DBGTOOL_COL_W - 12.0f);
    if (state) DrawText(x + 6.0f, y + 11.0f, 1.0f, state, accent);
}

/* A cell whose left and right halves do different things (DebugCellRightZoneHit
 * splits them at COL_W-46). Draws the label, the left state clipped to the
 * left half, a divider, and the right state in the right ~44px zone -- so it
 * is obvious at a glance that the cell has two tap targets. */
static void DrawDebugCellSplit(int index, const char* label,
                               const char* leftTxt, uint32_t leftCol,
                               const char* rightTxt, uint32_t rightCol) {
    float x = DBGTOOL_CELL_X(index & 1);
    float y = DBGTOOL_CELL_Y(index >> 1);
    float bx = x + (float)DBGTOOL_COL_W - 46.0f; /* divider / right-zone edge */
    DrawButtonBox(x, y, (float)DBGTOOL_COL_W, (float)DBGTOOL_CELL_H,
                  C2D_Color32(24, 32, 50, 255), C2D_Color32(50, 80, 130, 255));
    DrawTextMaxWClipped(x + 6.0f, y + 2.0f, 1.0f, label, C2D_Color32(255, 255, 255, 255),
                        0.0f, 240.0f, bx - x - 8.0f);
    if (leftTxt)
        DrawTextMaxWClipped(x + 6.0f, y + 11.0f, 1.0f, leftTxt, leftCol, 0.0f, 240.0f, bx - x - 8.0f);
    C2D_DrawRectSolid(bx - 1.0f, y + 3.0f, 0.92f, 1.0f, (float)DBGTOOL_CELL_H - 6.0f,
                      C2D_Color32(90, 110, 150, 255));
    /* Centred in the right zone (bx .. x+COL_W), horizontally and
     * vertically -- one short tag, mid-cell (glyphs are 7px in a 19px
     * cell). ~45px fits 7 glyphs at 6px each, so keep right tags short. */
    if (rightTxt)
        DrawTextCentered(bx + ((float)DBGTOOL_COL_W - (bx - x)) * 0.5f,
                         y + ((float)DBGTOOL_CELL_H - 7.0f) * 0.5f, 1.0f, rightTxt, rightCol);
}

/* The right ~48px of a cell is a start/stop side button (DebugCellRightZoneHit):
 * a divider, a tinted panel, and either a play triangle (idle) or a stop
 * square (running). The button's colour is the only running indicator -- the
 * cell's own label/state stays put. Drawn over the cell after it. */
static void DrawDebugCellSideButton(int index, bool running) {
    float xr = DBGTOOL_CELL_X(index & 1) + (float)DBGTOOL_COL_W;
    float y = DBGTOOL_CELL_Y(index >> 1);
    float cy = y + (float)DBGTOOL_CELL_H * 0.5f;
    float bx = xr - 46.0f;                 /* panel left edge */
    const uint32_t fg   = running ? C2D_Color32(255, 90, 90, 255)   /* red = tap to stop */
                                  : C2D_Color32(120, 230, 140, 255);/* green = tap to start */
    const uint32_t bg   = running ? C2D_Color32(70, 22, 22, 255)
                                  : C2D_Color32(22, 44, 30, 255);
    C2D_DrawRectSolid(bx - 2.0f, y + 3.0f, 0.92f, 1.0f, (float)DBGTOOL_CELL_H - 6.0f,
                      C2D_Color32(90, 110, 150, 255));
    C2D_DrawRectSolid(bx, y + 3.0f, 0.92f, 44.0f, (float)DBGTOOL_CELL_H - 6.0f, bg);
    float mx = bx + 22.0f;                 /* glyph centre */
    if (running) {
        C2D_DrawRectSolid(mx - 5.0f, cy - 5.0f, 0.93f, 10.0f, 10.0f, fg);
    } else {
        C2D_DrawTriangle(mx - 4.0f, cy - 6.0f, fg,
                         mx - 4.0f, cy + 6.0f, fg,
                         mx + 6.0f, cy,        fg, 0.93f);
    }
}

/* Stream the LOG A SD cell has selected (never NONE -- on/off is the side
 * button). Middle taps cycle it; when logging is on it is applied live. */
static PortDebugLogMode sLogSel = PORT_LOG_MODE_ALL;

static const char* DebugLogSelName(void) {
    switch (sLogSel) {
        case PORT_LOG_MODE_GPU:   return "GPU";
        case PORT_LOG_MODE_AUDIO: return "AUDIO";
        case PORT_LOG_MODE_PERF:  return "PERF";
        default:                  return "ALL";
    }
}

/* Index of the grid cell a tap landed on, or -1. */
static int DebugCellHit(int x, int y, int count) {
    int col;
    if (x >= DBGTOOL_COL_L_X && x <= DBGTOOL_COL_L_X + DBGTOOL_COL_W) col = 0;
    else if (x >= DBGTOOL_COL_R_X && x <= DBGTOOL_COL_R_X + DBGTOOL_COL_W) col = 1;
    else return -1;
    for (int r = 0; r < DBGTOOL_GRID_ROWS; ++r) {
        int cy = DBGTOOL_GRID_Y0 + r * DBGTOOL_GRID_PITCH;
        if (y < cy || y > cy + DBGTOOL_CELL_H) continue;
        int idx = r * 2 + col;
        return (idx < count) ? idx : -1;
    }
    return -1;
}

/* Single-row helper, still used by the warp submenu's spinner rows. */
#define DBGTOOL_ROW_X0    16
#define DBGTOOL_ROW_X1    304
#define DBGTOOL_ROW_Y0    48
#define DBGTOOL_ROW_PITCH 21
#define DBGTOOL_ROW_H     20
#define DBGTOOL_ROW_Y(i) ((float)(DBGTOOL_ROW_Y0 + (i) * DBGTOOL_ROW_PITCH))

static void DrawDebugRow(int i, const char* label, const char* value, uint32_t valueCol) {
    float ry = DBGTOOL_ROW_Y(i);
    DrawButtonBox((float)DBGTOOL_ROW_X0, ry,
                  (float)(DBGTOOL_ROW_X1 - DBGTOOL_ROW_X0), (float)DBGTOOL_ROW_H,
                  C2D_Color32(24, 32, 50, 255), C2D_Color32(60, 90, 130, 255));
    DrawText((float)DBGTOOL_ROW_X0 + 8.0f, ry + 6.0f, 1.0f, label, C2D_Color32(255, 255, 255, 255));
    if (value) DrawText(224.0f, ry + 6.0f, 1.0f, value, valueCol);
}

static int DebugRowHit(int x, int y, int rowCount) {
    if (x < DBGTOOL_ROW_X0 || x > DBGTOOL_ROW_X1) return -1;
    for (int i = 0; i < rowCount; ++i) {
        int ry = DBGTOOL_ROW_Y0 + i * DBGTOOL_ROW_PITCH;
        if (y >= ry && y <= ry + DBGTOOL_ROW_H) return i;
    }
    return -1;
}

static void DrawDebugModalFrame(int lang, const char* titleEs, const char* titleEn) {
    C2D_DrawRectSolid(10.0f, 26.0f, 0.85f, 300.0f, 210.0f, C2D_Color32(10, 14, 24, 250));
    C2D_DrawRectSolid(10.0f, 26.0f, 0.84f, 300.0f, 210.0f, C2D_Color32(40, 70, 120, 255));
    DrawText(20.0f, 32.0f, 1.0f, (lang == 6) ? titleEs : titleEn, C2D_Color32(255, 215, 0, 255));
    DrawButton(116.0f, 214.0f, 88.0f, 20.0f, (lang == 6) ? "CERRAR" : "CLOSE",
               C2D_Color32(255, 255, 255, 255), BTN_BLUE_BODY, BTN_BLUE_BORDER);
}

static bool DebugCloseHit(int x, int y) {
    return (x >= 100 && x <= 220 && y >= 214 && y <= 234);
}

static void RenderDebugToolsModal(int lang) {
    DrawDebugModalFrame(lang, "HERRAMIENTAS DE DEPURACION", "DEBUG TOOLS");

    const bool rec = PlatformGpu3DS_IsRecording();
    const bool perf = PlatformGpu3DS_IsPerfRecording();
    const char* onTxt = (lang == 6) ? "ACTIVO" : "ON";
    const char* offTxt = (lang == 6) ? "PARADO" : "OFF";
    const uint32_t colAct = C2D_Color32(140, 170, 210, 255);
    const uint32_t colOn = C2D_Color32(80, 255, 120, 255);
    const uint32_t colMenu = C2D_Color32(255, 215, 0, 255);

    DrawDebugCell(0, (lang == 6) ? "VOLCADO PANTALLA" : "SCREEN DUMP",
                  (lang == 6) ? "VOLCAR" : "DUMP", colAct);
    DrawDebugCell(1, (lang == 6) ? "MARCA EN EL LOG" : "LOG MARKER",
                  (lang == 6) ? "MARCAR" : "MARK", colAct);
    /* Middle taps the state line to cycle the capture preset (always shown
     * in yellow); the side button starts/stops -- its colour is the running
     * indicator, so the cell text never changes. */
    DrawDebugCell(2, (lang == 6) ? "GRAB. ESCENA" : "SCENE RECORDER",
                  PlatformGpu3DS_RecordPresetLabel(), colMenu);
    DrawDebugCellSideButton(2, rec);
    DrawDebugCell(3, (lang == 6) ? "GRAB. RENDIMIENTO" : "PERF RECORDER",
                  perf ? onTxt : offTxt, perf ? colOn : colAct);
    DrawDebugCell(4, (lang == 6) ? "ATLAS GPU" : "GPU ATLAS",
                  (lang == 6) ? "VOLCAR" : "DUMP", colAct);
    DrawDebugCell(5, (lang == 6) ? "MATAR A SAMUS" : "KILL SAMUS",
                  (lang == 6) ? "MATAR" : "KILL", C2D_Color32(255, 130, 130, 255));
    DrawDebugCell(6, (lang == 6) ? "TELETRANSPORTE" : "WARP", ">", colMenu);

    /* Nothing is written to the SD card while LOG is stopped even in a debug
     * build (see port_debug_log.h). Tapping the middle of LOG A SD cycles
     * the stream filter -- OFF / ALL / GPU / AUDIO / PERF (the label keeps
     * showing the pick); the side button starts/stops logging and its
     * colour is the running indicator. BUFFER is "stop holding lines in
     * RAM" for a hang. */
    const bool logOn = Port_DebugLog_IsEnabled();
    const bool logBuf = Port_DebugLog_IsBuffered();
    DrawDebugCell(7, (lang == 6) ? "LOG A SD" : "SD LOGGING",
                  DebugLogSelName(), colMenu);
    DrawDebugCellSideButton(7, logOn);
    DrawDebugCell(8, (lang == 6) ? "LOG EN BUFFER" : "LOG BUFFERING",
                  logBuf ? onTxt : offTxt, logBuf ? colOn : colAct);

#ifdef PORT_GPU_TILE_RENDERER
    /* Force the whole top screen through the CPU scanline renderer
     * (port/ppu/src/mode1.c) instead of the PICA200 tile renderer, to
     * isolate renderer-specific bugs without a RENDERER=cpu rebuild. The
     * per-frame CanRenderFrame() fallback still applies on top of this. */
    {
        extern bool Port_Bios_AdaptiveFrameSkipEnabled(void);
        const bool gpuOn = Port_GpuRenderer_IsActive();
        const bool skipOn = Port_Bios_AdaptiveFrameSkipEnabled();
        /* Left: GPU/CPU renderer. Right: adaptive render-skip (game speed
         * stays correct when frames overrun -- judder, not slow motion). */
        DrawDebugCellSplit(9, "RENDERER", gpuOn ? "GPU" : "CPU", gpuOn ? colAct : colOn,
                           skipOn ? "SKIP" : "skip",
                           skipOn ? C2D_Color32(140, 235, 150, 255) : C2D_Color32(120, 135, 160, 255));
    }
    {
        /* Run the Old3DS profile on New3DS hardware without a FORCE_OLD3DS
         * rebuild. Locked on a real Old3DS (nothing to force). */
        {
            extern bool Platform3DS_HardwareIsNew3DS(void);
            extern bool Platform3DS_ForcedOld3DSProfile(void);
            const bool hwNew = Platform3DS_HardwareIsNew3DS();
            const bool forced = Platform3DS_ForcedOld3DSProfile();
            const char* st = !hwNew ? "OLD (hw)" : (forced ? "OLD (forz.)" : "NEW");
            DrawDebugCell(10, "PERFIL 3DS", st,
                          !hwNew ? C2D_Color32(120, 135, 160, 255)
                                 : (forced ? C2D_Color32(230, 200, 120, 255)
                                           : C2D_Color32(120, 230, 140, 255)));
        }
        /* Flat-colour every layer/sprite by its stereo depth plane, so a
         * wrongly-placed cutscene layer stands out at a glance. */
        {
            const bool dt = Port_GpuRenderer_DepthTintEnabled();
            DrawDebugCell(11, (lang == 6) ? "PROFUNDIDAD" : "DEPTH TINT",
                          dt ? onTxt : offTxt,
                          dt ? colOn : colAct);
        }
    }
#endif

    {
        /* The SD card over the network while the game runs (ftp_server_3ds.h). The side button
         * starts or stops it; the port is always 5000, so the cell only needs the IP. */
        unsigned sent = 0, received = 0;
        bool connected = false;
        char address[40];
        FtpServer_Stats(&sent, &received, &connected);
        FtpServer_Address(address, sizeof(address));
        char* port = strstr(address, ":5000");
        if (port) *port = 0;
        const bool running = FtpServer_Running();
        const bool waiting = !running && Port_Config_DebugFtpOn();
        DrawDebugCell(DBGTOOL_FTP_CELL, connected ? "FTP  CLIENT ON" : "FTP SERVER",
                      waiting ? "WAITING WI-FI" : address,
                      running ? colOn : waiting ? C2D_Color32(240, 200, 80, 255) : colAct);
        DrawDebugCellSideButton(DBGTOOL_FTP_CELL, running || waiting);
    }

    DrawDebugCell(DBGTOOL_SOUNDTEST_CELL, (lang == 6) ? "SOUND TEST" : "SOUND TEST", ">", colMenu);

    if (sDebugToolsMsg[0] && sFrameCounter < sDebugToolsMsgUntil) {
        DrawTextCentered(160.0f, 204.0f, 1.0f, sDebugToolsMsg, C2D_Color32(120, 255, 160, 255));
    } else {
        /* Linear heap, free / total. Everything big the port reserves comes
         * out of here -- the 8MB ROM buffer, the tile atlas, the citro2d/
         * citro3d buffers, the bezel and effect textures -- so this is the
         * number that decides whether the port fits in whatever memory the
         * loader gave us. A CIA declares its own budget (SystemMode in
         * cia/mzm3ds.rsf); a .3dsx inherits the Homebrew Launcher host's
         * leftovers, which is why the same build can run installed and run
         * out of memory launched as homebrew. Read it here before blaming
         * anything else for an allocation failure. */
        PlatformGpu3DSStats st;
        PlatformGpu3DS_GetStats(&st);
        /* Plain unsigned, not u32/uint32_t: those are unsigned long on
         * devkitARM and would not match snprintf's %u. */
        const unsigned freeKb = (unsigned)(linearSpaceFree() / 1024u);
        const unsigned totalKb = (unsigned)(st.linearHeapBytes / 1024u);
        char mem[48];
        snprintf(mem, sizeof(mem), "LINEAR %u.%01u / %u.%01u MB FREE",
                 freeKb / 1024u, (freeKb % 1024u) * 10u / 1024u,
                 totalKb / 1024u, (totalKb % 1024u) * 10u / 1024u);
        DrawTextCentered(160.0f, 204.0f, 1.0f, mem, C2D_Color32(140, 170, 210, 255));
    }
}

/* True when the tap is in the right ~48px of grid cell `cell` -- the
 * start/stop side button (DrawDebugCellSideButton). The rest of the cell
 * cycles that cell's option. */
static bool DebugCellRightZoneHit(int x, int cell) {
    float cx = DBGTOOL_CELL_X(cell & 1);
    return (float)x >= cx + (float)DBGTOOL_COL_W - 48.0f;
}

static bool HandleDebugToolsModalTouch(int x, int y) {
    if (DebugCloseHit(x, y)) {
        sShowDebugToolsModal = false;
        return true;
    }
    int cell = DebugCellHit(x, y, DBGTOOL_COUNT);
    /* SCENE RECORDER / SD LOGGING: the side button starts/stops; the rest of
     * the cell cycles the option. */
    if (cell == 2 && DebugCellRightZoneHit(x, 2)) {
        PlatformGpu3DS_ToggleRecording();
        const char* last = PlatformGpu3DS_RecordLastFile();
        DebugToolsSetMsg(PlatformGpu3DS_IsRecording() ? "REC ON"
                         : (last && last[0]) ? last : "REC OFF");
        return true;
    }
    if (cell == 7 && DebugCellRightZoneHit(x, 7)) {
        /* Side button: start/stop logging on the selected stream. */
        if (Port_DebugLog_IsEnabled()) {
            Port_DebugLog_SetMode(PORT_LOG_MODE_NONE);
            DebugToolsSetMsg("LOG SD: OFF");
        } else {
            Port_DebugLog_SetMode(sLogSel);
            Port_DebugLog("USER MARK: SD logging enabled");
            char msg[24];
            snprintf(msg, sizeof(msg), "LOG SD: %s", DebugLogSelName());
            DebugToolsSetMsg(msg);
        }
        return true;
    }
    switch (cell) {
        case 0:
            PlatformGpu3DS_DumpScreens();
            DebugToolsSetMsg("DUMP -> debug/");
            break;
        case 1:
            Port_DebugLog("USER MARK: debug tools menu");
            DebugToolsSetMsg("MARCA EN EL LOG");
            break;
        case 2:
            /* Middle: cycle the capture preset (no-op while recording). */
            PlatformGpu3DS_CycleRecordPreset();
            DebugToolsSetMsg(PlatformGpu3DS_RecordPresetLabel());
            break;
        case 3:
            PlatformGpu3DS_TogglePerfRecording();
            DebugToolsSetMsg(PlatformGpu3DS_IsPerfRecording() ? "PERF ON" : "PERF OFF");
            break;
        case 4:
            Port_GpuRenderer_DumpAtlas(PORT_DEBUG_DIR "/mzm-live-atlas.ppm", PORT_DEBUG_DIR "/mzm-live-atlas-keys.csv");
            DebugToolsSetMsg("ATLAS -> debug/");
            break;
        case 5:
            PortPpuMzm_DebugKillSamus();
            DebugToolsSetMsg("SAMUS MUERTA");
            break;
        case 6:
            sShowDebugToolsModal = false;
            sShowDebugWarpModal = true;
            break;
        case 7: {
            /* Middle: cycle the stream selection (ALL/GPU/AUDIO/PERF, no
             * OFF -- the side button does on/off). Applied live if logging
             * is running. */
            sLogSel = (PortDebugLogMode)(sLogSel + 1);
            if (sLogSel >= PORT_LOG_MODE_COUNT) sLogSel = PORT_LOG_MODE_ALL;
            if (Port_DebugLog_IsEnabled()) Port_DebugLog_SetMode(sLogSel);
            char msg[24];
            snprintf(msg, sizeof(msg), "LOG SD: %s", DebugLogSelName());
            DebugToolsSetMsg(msg);
            break;
        }
        case 8: {
            const bool buf = !Port_DebugLog_IsBuffered();
            Port_DebugLog_SetBuffered(buf);
            DebugToolsSetMsg(buf ? "LOG EN BUFFER" : "LOG DIRECTO");
            break;
        }
#ifdef PORT_GPU_TILE_RENDERER
        case 9: {
            extern void Port_Bios_SetAdaptiveFrameSkip(bool on);
            extern bool Port_Bios_AdaptiveFrameSkipEnabled(void);
            if (DebugCellRightZoneHit(x, 9)) {
                const bool on = !Port_Bios_AdaptiveFrameSkipEnabled();
                Port_Bios_SetAdaptiveFrameSkip(on);
                DebugToolsSetMsg(on ? "FRAME-SKIP ADAPTATIVO: ON"
                                    : "FRAME-SKIP ADAPTATIVO: OFF");
            } else {
                const bool gpuOn = !Port_GpuRenderer_IsActive();
                Port_GpuRenderer_SetActive(gpuOn);
                Port_DebugLog(gpuOn ? "USER MARK: renderer -> GPU"
                                    : "USER MARK: renderer -> CPU");
                DebugToolsSetMsg(gpuOn ? "RENDERER: GPU" : "RENDERER: CPU");
            }
            break;
        }
        case 10: {
            extern bool Platform3DS_HardwareIsNew3DS(void);
            extern bool Platform3DS_ForcedOld3DSProfile(void);
            extern void Platform3DS_SetForcedOld3DSProfile(bool forced);
            if (!Platform3DS_HardwareIsNew3DS()) {
                DebugToolsSetMsg("PERFIL 3DS: OLD (hardware, fijo)");
            } else {
                const bool forced = !Platform3DS_ForcedOld3DSProfile();
                Platform3DS_SetForcedOld3DSProfile(forced);
                DebugToolsSetMsg(forced ? "PERFIL 3DS: OLD (forzado)"
                                        : "PERFIL 3DS: NEW");
            }
            break;
        }
        case 11: {
            const bool on = !Port_GpuRenderer_DepthTintEnabled();
            Port_GpuRenderer_SetDepthTint(on);
            DebugToolsSetMsg(on ? "TINTE DE PROFUNDIDAD: ON"
                                : "TINTE DE PROFUNDIDAD: OFF");
            break;
        }
#endif
        case DBGTOOL_FTP_CELL: {
            /* Side button: start / stop (also cancels a start still waiting for Wi-Fi). The rest
             * of the cell says the address again. */
            char msg[48], address[40];
            if (DebugCellRightZoneHit(x, DBGTOOL_FTP_CELL)) {
                if (FtpServer_Running() || Port_Config_DebugFtpOn()) {
                    FtpServer_Stop();
                    Port_Config_SetDebugFtpOn(false);
                    DebugToolsSetMsg("FTP STOPPED");
                } else {
                    Port_Config_SetDebugFtpOn(true);
                    const bool ok = FtpServer_Start();
                    FtpServer_Address(address, sizeof(address));
                    snprintf(msg, sizeof(msg), ok ? "FTP %s" : "FTP: %s (RETRYING)", address);
                    DebugToolsSetMsg(msg);
                }
            } else if (FtpServer_Running()) {
                FtpServer_Address(address, sizeof(address));
                snprintf(msg, sizeof(msg), "FTP %s", address);
                DebugToolsSetMsg(msg);
            }
            break;
        }
        case DBGTOOL_SOUNDTEST_CELL:
            sShowDebugToolsModal = false;
            sShowSoundTestModal = true;
            break;
        default:
            break;
    }
    return true;
}

/* Warp submenu. Jumps are addressed by (area, door id) -- see the long
 * comment on the warp block in port_ppu_mzm.c for why a door and not a room
 * number plus coordinates. The door spinner shows which room each door
 * leads into, so a room can be found by stepping doors without knowing any
 * door ids beforehand. */
#define DBGWARP_ROW_COUNT 5

static void RenderDebugWarpModal(int lang) {
    DrawDebugModalFrame(lang, "TELETRANSPORTE", "WARP");

    int selArea = 0, selDoor = 0, selRoom = -1;
    PortPpuMzm_DebugGetWarpSelection(&selArea, &selDoor, &selRoom);
    const int doorCount = PortPpuMzm_DebugGetDoorCount(selArea);
    const bool hasWarp = PortPpuMzm_DebugHasWarpPoint();

    char valBuf[24];

    /* Row 0/1: the spinner. The whole row is a hit target split in three:
     * left third = previous, right third = next, middle = nothing, so a
     * mis-tap in the middle does nothing rather than jumping a step. */
    DrawDebugRow(0, (lang == 6) ? "AREA" : "AREA", NULL, 0);
    DrawText(120.0f, DBGTOOL_ROW_Y(0) + 6.0f, 1.0f, "<", C2D_Color32(255, 215, 0, 255));
    DrawText(140.0f, DBGTOOL_ROW_Y(0) + 6.0f, 1.0f, AreaName((uint8_t)selArea), C2D_Color32(220, 235, 255, 255));
    DrawText(288.0f, DBGTOOL_ROW_Y(0) + 6.0f, 1.0f, ">", C2D_Color32(255, 215, 0, 255));

    snprintf(valBuf, sizeof(valBuf), "%d / %d", selDoor, doorCount > 0 ? doorCount - 1 : 0);
    DrawDebugRow(1, (lang == 6) ? "PUERTA" : "DOOR", NULL, 0);
    DrawText(120.0f, DBGTOOL_ROW_Y(1) + 6.0f, 1.0f, "<", C2D_Color32(255, 215, 0, 255));
    DrawText(140.0f, DBGTOOL_ROW_Y(1) + 6.0f, 1.0f, valBuf, C2D_Color32(220, 235, 255, 255));
    DrawText(288.0f, DBGTOOL_ROW_Y(1) + 6.0f, 1.0f, ">", C2D_Color32(255, 215, 0, 255));

    if (selRoom >= 0) snprintf(valBuf, sizeof(valBuf), "%s %d", (lang == 6) ? "SALA" : "ROOM", selRoom);
    else snprintf(valBuf, sizeof(valBuf), "--");
    DrawDebugRow(2, (lang == 6) ? "IR A ESA PUERTA" : "GO TO THAT DOOR",
                 valBuf, selRoom >= 0 ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(110, 120, 140, 255));

    DrawDebugRow(3, (lang == 6) ? "GUARDAR PUNTO AQUI" : "SAVE POINT HERE",
                 (lang == 6) ? "GUARDAR" : "SAVE", C2D_Color32(255, 215, 0, 255));
    DrawDebugRow(4, (lang == 6) ? "IR AL PUNTO GUARDADO" : "GO TO SAVED POINT",
                 hasWarp ? ((lang == 6) ? "IR" : "GO") : "--",
                 hasWarp ? C2D_Color32(80, 255, 120, 255) : C2D_Color32(110, 120, 140, 255));

    char warpInfo[48];
    PortPpuMzm_DebugGetWarpPointInfo(warpInfo, (int)sizeof(warpInfo));
    DrawText((float)DBGTOOL_ROW_X0, 176.0f, 1.0f, warpInfo, C2D_Color32(150, 190, 240, 255));

    /* Current position, so "save point here" is verifiable before pressing
     * it and the spinner has something to aim at. */
    char hereBuf[64];
    snprintf(hereBuf, sizeof(hereBuf), "%s: %s %s %u",
             (lang == 6) ? "AHORA" : "NOW", AreaName(gCurrentArea),
             (lang == 6) ? "SALA" : "ROOM", gCurrentRoom);
    DrawText((float)DBGTOOL_ROW_X0, 187.0f, 1.0f, hereBuf, C2D_Color32(150, 190, 240, 255));

    if (sDebugToolsMsg[0] && sFrameCounter < sDebugToolsMsgUntil) {
        DrawTextCentered(160.0f, 198.0f, 1.0f, sDebugToolsMsg, C2D_Color32(120, 255, 160, 255));
    }
}

static void HandleDebugWarpModalTouch(int x, int y) {
    if (DebugCloseHit(x, y)) {
        sShowDebugWarpModal = false;
        sShowDebugToolsModal = true;   /* back to the parent grid, not the tab */
        return;
    }
    int row = DebugRowHit(x, y, DBGWARP_ROW_COUNT);
    const int step = (x < 140) ? -1 : (x > 270 ? 1 : 0);
    switch (row) {
        case 0:
            if (step) PortPpuMzm_DebugStepWarpArea(step);
            break;
        case 1:
            if (step) PortPpuMzm_DebugStepWarpDoor(step);
            break;
        case 2:
            /* The jump itself is deferred to src/agbmain.c's loop (see
             * PortPpuMzm_DebugApplyPendingWarp). Closing the modal so the
             * top screen is watchable the moment the room reloads. */
            if (PortPpuMzm_DebugRequestWarpToSelection()) {
                DebugToolsSetMsg("TELETRANSPORTANDO...");
                sShowDebugWarpModal = false;
            } else {
                DebugToolsSetMsg("PUERTA NO VALIDA");
            }
            break;
        case 3:
            PortPpuMzm_DebugSaveWarpPoint();
            DebugToolsSetMsg("PUNTO GUARDADO");
            break;
        case 4:
            if (PortPpuMzm_DebugRequestWarp()) {
                DebugToolsSetMsg("TELETRANSPORTANDO...");
                sShowDebugWarpModal = false;
            } else {
                DebugToolsSetMsg("NO HAY PUNTO GUARDADO");
            }
            break;
        default:
            break;
    }
}

/* SOUND TEST submenu: audition any engine sound id (0x00..0x2C3) plus the
 * custom RA unlock sting, straight from the console. DEBUG_TOOLS only, no
 * state persisted. Sound ids: include/constants/audio.h. */
extern void SoundPlay(uint16_t sound);
extern void StopAllMusicAndSounds(void);
extern void Port_MzmAudio_PlayRaUnlockSound(void);
extern void Port_MzmAudio_StopRaUnlockSound(void);
extern bool Port_MzmAudio_IsRaUnlockSoundPlaying(void);

#define SNDTEST_MAX_ID   0x2C3
#define SNDTEST_ROW_COUNT 4
static int sSndTestId = 0x37; /* MUSIC_GETTING_ITEM_JINGLE */
static bool sSndTestEnginePlaying = false;

static void SndTestStep(int d) {
    int v = sSndTestId + d;
    if (v < 0) v = 0;
    if (v > SNDTEST_MAX_ID) v = SNDTEST_MAX_ID;
    sSndTestId = v;
}

static void RenderSoundTestModal(int lang) {
    DrawDebugModalFrame(lang, "SOUND TEST", "SOUND TEST");

    char buf[40];
    snprintf(buf, sizeof(buf), "0x%03X  (%d)", sSndTestId, sSndTestId);
    DrawDebugRow(0, "ID", NULL, 0);
    DrawText(120.0f, DBGTOOL_ROW_Y(0) + 6.0f, 1.0f, "<", C2D_Color32(255, 215, 0, 255));
    DrawText(140.0f, DBGTOOL_ROW_Y(0) + 6.0f, 1.0f, buf, C2D_Color32(220, 235, 255, 255));
    DrawText(288.0f, DBGTOOL_ROW_Y(0) + 6.0f, 1.0f, ">", C2D_Color32(255, 215, 0, 255));

    DrawDebugRow(1, "ID  x16", NULL, 0);
    DrawText(120.0f, DBGTOOL_ROW_Y(1) + 6.0f, 1.0f, "<<", C2D_Color32(255, 215, 0, 255));
    DrawText(286.0f, DBGTOOL_ROW_Y(1) + 6.0f, 1.0f, ">>", C2D_Color32(255, 215, 0, 255));

    /* One toggle per source: tap plays it, tap again stops it. The glyph is
     * a play triangle when idle and a stop square while it is sounding. */
    const uint32_t glyphIdle = C2D_Color32(120, 255, 140, 255);
    const uint32_t glyphLive = C2D_Color32(255, 120, 120, 255);

    DrawDebugRow(2, (lang == 6) ? "SONIDO DEL MOTOR" : "ENGINE SOUND", NULL, 0);
    DrawActionGlyph(292.0f, DBGTOOL_ROW_Y(2) + (float)DBGTOOL_ROW_H * 0.5f,
                    sSndTestEnginePlaying, sSndTestEnginePlaying ? glyphLive : glyphIdle);

    bool raLive = Port_MzmAudio_IsRaUnlockSoundPlaying();
    DrawDebugRow(3, (lang == 6) ? "SONIDO DE LOGRO (RA)" : "RA UNLOCK SOUND", NULL, 0);
    DrawActionGlyph(292.0f, DBGTOOL_ROW_Y(3) + (float)DBGTOOL_ROW_H * 0.5f,
                    raLive, raLive ? glyphLive : glyphIdle);

    DrawText((float)DBGTOOL_ROW_X0, 176.0f, 1.0f,
             (lang == 6) ? "0x01-0x5F musica/jingles  |  0x60+ efectos"
                         : "0x01-0x5F music/jingles  |  0x60+ SFX",
             C2D_Color32(150, 190, 240, 255));

    if (sDebugToolsMsg[0] && sFrameCounter < sDebugToolsMsgUntil)
        DrawTextCentered(160.0f, 190.0f, 1.0f, sDebugToolsMsg, C2D_Color32(120, 255, 160, 255));
}

static void HandleSoundTestModalTouch(int x, int y) {
    if (DebugCloseHit(x, y)) {
        sShowSoundTestModal = false;
        sShowDebugToolsModal = true;   /* back to the parent grid */
        return;
    }
    int row = DebugRowHit(x, y, SNDTEST_ROW_COUNT);
    const int dir = (x < 140) ? -1 : (x > 270 ? 1 : 0);
    char m[24];
    switch (row) {
        case 0: if (dir) { SndTestStep(dir); sSndTestEnginePlaying = false; } break;
        case 1: if (dir) { SndTestStep(dir * 16); sSndTestEnginePlaying = false; } break;
        case 2:
            if (sSndTestEnginePlaying) {
                StopAllMusicAndSounds();
                sSndTestEnginePlaying = false;
                DebugToolsSetMsg("STOP");
            } else {
                SoundPlay((uint16_t)sSndTestId);
                sSndTestEnginePlaying = true;
                snprintf(m, sizeof(m), "PLAY 0x%03X", sSndTestId);
                DebugToolsSetMsg(m);
            }
            break;
        case 3:
            if (Port_MzmAudio_IsRaUnlockSoundPlaying()) {
                Port_MzmAudio_StopRaUnlockSound();
                DebugToolsSetMsg("STOP");
            } else {
                Port_MzmAudio_PlayRaUnlockSound();
                DebugToolsSetMsg("RA SOUND");
            }
            break;
        default: break;
    }
}

/* Equipment editor. Every flag is a two-state chip in a two-column grid;
 * the bottom rows are the bulk actions. The bit constants are duplicated
 * from include/constants/samus.h (the BBF_ and SMF_ enums) rather than
 * included,
 * because this file can't pull in the GBA-side headers -- same <3ds.h>
 * typedef clash that keeps all the game-state pokes in port_ppu_mzm.c. */
#define DBG_BBF_LONG_BEAM   (1u << 0)
#define DBG_BBF_ICE_BEAM    (1u << 1)
#define DBG_BBF_WAVE_BEAM   (1u << 2)
#define DBG_BBF_PLASMA_BEAM (1u << 3)
#define DBG_BBF_CHARGE_BEAM (1u << 4)
#define DBG_BBF_BOMBS       (1u << 7)

#define DBG_SMF_HIGH_JUMP    (1u << 0)
#define DBG_SMF_SPEEDBOOSTER (1u << 1)
#define DBG_SMF_SPACE_JUMP   (1u << 2)
#define DBG_SMF_SCREW_ATTACK (1u << 3)
#define DBG_SMF_VARIA_SUIT   (1u << 4)
#define DBG_SMF_GRAVITY_SUIT (1u << 5)
#define DBG_SMF_MORPH_BALL   (1u << 6)
#define DBG_SMF_POWER_GRIP   (1u << 7)

/* isBeam picks which of the two equipment bytes the bit belongs to. */
struct DebugEquipEntry { const char* es; const char* en; unsigned bit; bool isBeam; };

static const struct DebugEquipEntry kDebugEquipLeft[] = {
    { "LARGO",   "LONG",   DBG_BBF_LONG_BEAM,   true },
    { "HIELO",   "ICE",    DBG_BBF_ICE_BEAM,    true },
    { "ONDA",    "WAVE",   DBG_BBF_WAVE_BEAM,   true },
    { "PLASMA",  "PLASMA", DBG_BBF_PLASMA_BEAM, true },
    { "CARGA",   "CHARGE", DBG_BBF_CHARGE_BEAM, true },
    { "BOMBAS",  "BOMBS",  DBG_BBF_BOMBS,       true },
    { "MORFO",   "MORPH",  DBG_SMF_MORPH_BALL,  false },
};

static const struct DebugEquipEntry kDebugEquipRight[] = {
    { "SALTO ALTO", "HIGH JUMP",  DBG_SMF_HIGH_JUMP,    false },
    { "TURBO",      "SPEED",      DBG_SMF_SPEEDBOOSTER, false },
    { "ESPACIAL",   "SPACE JUMP", DBG_SMF_SPACE_JUMP,   false },
    { "TORNILLO",   "SCREW",      DBG_SMF_SCREW_ATTACK, false },
    { "VARIA",      "VARIA",      DBG_SMF_VARIA_SUIT,   false },
    { "GRAVEDAD",   "GRAVITY",    DBG_SMF_GRAVITY_SUIT, false },
    { "AGARRE",     "POWER GRIP", DBG_SMF_POWER_GRIP,   false },
};

#define DBGEQUIP_ROWS      7
#define DBGEQUIP_COL_W     140
#define DBGEQUIP_COL_L_X   16
#define DBGEQUIP_COL_R_X   164
#define DBGEQUIP_GRID_Y0   46
#define DBGEQUIP_GRID_PITCH 17
#define DBGEQUIP_CELL_H    16
/* Bulk-action rows, below the grid. The second one has to end before the
 * frame's close button at y=208, which is what the previous layout ran
 * into. */
#define DBGEQUIP_ACT_Y0    166
#define DBGEQUIP_ACT_H     18
#define DBGEQUIP_ACT2_Y0   186

static bool DebugEquipHas(const struct DebugEquipEntry* e, unsigned beams, unsigned misc) {
    return ((e->isBeam ? beams : misc) & e->bit) != 0;
}

static void DrawEquipCell(float x, float y, const struct DebugEquipEntry* e, int lang,
                          unsigned beams, unsigned misc) {
    bool on = DebugEquipHas(e, beams, misc);
    C2D_DrawRectSolid(x, y, 0.9f, (float)DBGEQUIP_COL_W, (float)DBGEQUIP_CELL_H,
                      on ? C2D_Color32(20, 60, 35, 255) : C2D_Color32(24, 32, 50, 255));
    DrawText(x + 6.0f, y + 5.0f, 1.0f, (lang == 6) ? e->es : e->en,
             on ? C2D_Color32(120, 255, 160, 255) : C2D_Color32(140, 150, 170, 255));
    DrawText(x + (float)DBGEQUIP_COL_W - 26.0f, y + 5.0f, 1.0f, on ? "ON" : "--",
             on ? C2D_Color32(120, 255, 160, 255) : C2D_Color32(110, 120, 140, 255));
}

static void DrawEquipAction(float x, float y, float w, const char* label, uint32_t col) {
    DrawButton(x, y, w, (float)DBGEQUIP_ACT_H, label, col,
               C2D_Color32(24, 40, 66, 255), C2D_Color32(70, 110, 160, 255));
}

static void RenderDebugEquipModal(int lang) {
    DrawDebugModalFrame(lang, "EQUIPO Y OBJETOS", "EQUIPMENT & ITEMS");

    unsigned beams = 0, misc = 0;
    PortPpuMzm_DebugGetEquipment(&beams, &misc);

    for (int i = 0; i < DBGEQUIP_ROWS; ++i) {
        float y = (float)(DBGEQUIP_GRID_Y0 + i * DBGEQUIP_GRID_PITCH);
        DrawEquipCell((float)DBGEQUIP_COL_L_X, y, &kDebugEquipLeft[i], lang, beams, misc);
        DrawEquipCell((float)DBGEQUIP_COL_R_X, y, &kDebugEquipRight[i], lang, beams, misc);
    }

    DrawEquipAction((float)DBGEQUIP_COL_L_X, (float)DBGEQUIP_ACT_Y0, 140.0f,
                    (lang == 6) ? "TODO ON" : "ALL ON", C2D_Color32(120, 255, 160, 255));
    DrawEquipAction((float)DBGEQUIP_COL_R_X, (float)DBGEQUIP_ACT_Y0, 140.0f,
                    (lang == 6) ? "TODO OFF" : "ALL OFF", C2D_Color32(255, 150, 150, 255));

    float ay = (float)DBGEQUIP_ACT2_Y0;
    DrawEquipAction((float)DBGEQUIP_COL_L_X, ay, 90.0f,
                    (lang == 6) ? "MUNIC MAX" : "AMMO MAX", C2D_Color32(255, 215, 0, 255));
    DrawEquipAction((float)(DBGEQUIP_COL_L_X + 94), ay, 90.0f,
                    (lang == 6) ? "MUNIC MIN" : "AMMO MIN", C2D_Color32(255, 150, 150, 255));
    DrawEquipAction((float)(DBGEQUIP_COL_L_X + 188), ay, 100.0f,
                    (lang == 6) ? "RELLENAR" : "REFILL", C2D_Color32(120, 255, 160, 255));

    /* Deliberately no energy/missile readout here: the game's own HUD on the
     * top screen already shows all of it, and the line collided with the
     * action buttons. */
}

static void HandleDebugEquipModalTouch(int x, int y) {
    if (DebugCloseHit(x, y)) {
        sShowDebugEquipModal = false;
        sShowDebugToolsModal = true;
        return;
    }

    /* Toggle grid */
    for (int i = 0; i < DBGEQUIP_ROWS; ++i) {
        int cy = DBGEQUIP_GRID_Y0 + i * DBGEQUIP_GRID_PITCH;
        if (y < cy || y > cy + DBGEQUIP_CELL_H) continue;
        const struct DebugEquipEntry* e = NULL;
        if (x >= DBGEQUIP_COL_L_X && x <= DBGEQUIP_COL_L_X + DBGEQUIP_COL_W) e = &kDebugEquipLeft[i];
        else if (x >= DBGEQUIP_COL_R_X && x <= DBGEQUIP_COL_R_X + DBGEQUIP_COL_W) e = &kDebugEquipRight[i];
        if (!e) return;
        if (e->isBeam) PortPpuMzm_DebugToggleBeam(e->bit);
        else PortPpuMzm_DebugToggleMisc(e->bit);
        return;
    }

    /* Bulk equipment rows */
    if (y >= DBGEQUIP_ACT_Y0 && y <= DBGEQUIP_ACT_Y0 + DBGEQUIP_ACT_H) {
        if (x >= DBGEQUIP_COL_L_X && x <= DBGEQUIP_COL_L_X + DBGEQUIP_COL_W) {
            PortPpuMzm_DebugSetAllEquipment(true);
            DebugToolsSetMsg("EQUIPO COMPLETO");
        } else if (x >= DBGEQUIP_COL_R_X && x <= DBGEQUIP_COL_R_X + DBGEQUIP_COL_W) {
            PortPpuMzm_DebugSetAllEquipment(false);
            DebugToolsSetMsg("EQUIPO VACIADO");
        }
        return;
    }

    int ay = DBGEQUIP_ACT2_Y0;
    if (y >= ay && y <= ay + DBGEQUIP_ACT_H) {
        if (x >= DBGEQUIP_COL_L_X && x < DBGEQUIP_COL_L_X + 94) {
            PortPpuMzm_DebugSetAmmo(true);
            DebugToolsSetMsg("MUNICION AL MAXIMO");
        } else if (x >= DBGEQUIP_COL_L_X + 94 && x < DBGEQUIP_COL_L_X + 188) {
            PortPpuMzm_DebugSetAmmo(false);
            DebugToolsSetMsg("MUNICION A CERO");
        } else if (x >= DBGEQUIP_COL_L_X + 188 && x <= DBGEQUIP_COL_L_X + 288) {
            PortPpuMzm_DebugRefillAmmo();
            DebugToolsSetMsg("RECARGADO");
        }
    }
}
#endif /* PORT_DEBUG_TOOLS_ACTIVE */

/* Render Debug View with Map Tile Inspector */
static void RenderDebugView(void) {
    C2D_DrawRectSolid(8.0f, 28.0f, 0.4f, 304.0f, 204.0f, C2D_Color32(14, 18, 28, 255));
    C2D_DrawRectSolid(8.0f, 28.0f, 0.35f, 304.0f, 204.0f, C2D_Color32(40, 60, 90, 255));

    DrawText(16.0f, 32.0f, 1.0f, "PERFORMANCE & MAP DEBUG (" MZM_PORT_VERSION ")", C2D_Color32(100, 220, 255, 255));

    char buf[80];
    double fps = Port_PPU_3DS_CurrentFps();
    unsigned roundedFps = fps > 0.0 ? (unsigned)(fps + 0.5) : 0u;
    if (roundedFps > 999u) roundedFps = 999u;
    const bool usedGpu = Port_PPU_3DS_LastFrameUsedGpu();
    const bool isNew3ds = Platform3DS_IsNew3DS();

    snprintf(buf, sizeof(buf), "FPS: %u   RENDER: %s   SYS: %s",
             roundedFps, usedGpu ? "GPU" : "CPU", isNew3ds ? "N3DS" : "O3DS");
    DrawText(16.0f, 46.0f, 1.0f, buf, C2D_Color32(255, 255, 255, 255));

    snprintf(buf, sizeof(buf), "GAME STATE: AREA=%s (%u) ROOM=%u",
             AreaName(gCurrentArea), gCurrentArea, gCurrentRoom);
    DrawText(16.0f, 58.0f, 1.0f, buf, C2D_Color32(180, 240, 160, 255));

    snprintf(buf, sizeof(buf), "MAP COORDS: X=%u Y=%u", gMinimapX, gMinimapY);
    DrawText(16.0f, 70.0f, 1.0f, buf, C2D_Color32(180, 240, 160, 255));

    /* Current room raw tile data & surrounding 3x3 inspect */
    uint16_t curTile = gDecompressedMinimapVisitedTiles[gMinimapX + gMinimapY * 32];
    uint16_t baseTile = gDecompressedMinimapData[gMinimapX + gMinimapY * 32];
    snprintf(buf, sizeof(buf), "CURRENT TILE: VISITED=0x%04X (T=%u P=%u)",
             curTile, curTile & 0x3FF, (curTile >> 12) & 0xF);
    DrawText(16.0f, 82.0f, 1.0f, buf, C2D_Color32(255, 220, 100, 255));

    snprintf(buf, sizeof(buf), "BASE MAP TILE: 0x%04X (T=%u P=%u)",
             baseTile, baseTile & 0x3FF, (baseTile >> 12) & 0xF);
    DrawText(16.0f, 93.0f, 1.0f, buf, C2D_Color32(255, 200, 80, 255));

    /* 3x3 Grid surrounding Samus */
    DrawText(16.0f, 107.0f, 1.0f, "SURROUNDING 3x3 TILES:", C2D_Color32(140, 200, 255, 255));
    for (int dy = -1; dy <= 1; ++dy) {
        int ty = (int)gMinimapY + dy;
        char rowBuf[64];
        if (ty >= 0 && ty < 32) {
            uint16_t t0 = ((int)gMinimapX - 1 >= 0) ? gDecompressedMinimapVisitedTiles[((int)gMinimapX - 1) + ty * 32] : 0;
            uint16_t t1 = gDecompressedMinimapVisitedTiles[gMinimapX + ty * 32];
            uint16_t t2 = ((int)gMinimapX + 1 < 32) ? gDecompressedMinimapVisitedTiles[((int)gMinimapX + 1) + ty * 32] : 0;
            snprintf(rowBuf, sizeof(rowBuf), "Y%+d: [0x%04X] [0x%04X] [0x%04X]", dy, t0, t1, t2);
        } else {
            snprintf(rowBuf, sizeof(rowBuf), "Y%+d: OUT OF BOUNDS", dy);
        }
        DrawText(20.0f, 118.0f + (float)(dy + 1) * 11.0f, 1.0f, rowBuf, C2D_Color32(200, 220, 240, 255));
    }

    /* Active Chozo target status */
    uint8_t tArea = 0, tX = 0, tY = 0;
    bool hasTarget = GetActiveChozoTarget(&tArea, &tX, &tY);
    if (hasTarget) {
        snprintf(buf, sizeof(buf), "CHOZO TARGET: AREA=%s (%u) X=%u Y=%u", AreaName(tArea), tArea, tX, tY);
        DrawText(16.0f, 158.0f, 1.0f, buf, C2D_Color32(255, 120, 120, 255));
    } else {
        DrawText(16.0f, 158.0f, 1.0f, "CHOZO TARGET: NONE ACTIVE", C2D_Color32(140, 150, 170, 255));
    }

    /* Check obtained items count */
    uint32_t totalItems = 0;
    for (int a = 0; a < 8; ++a) {
        for (int row = 0; row < 32; ++row) {
            uint32_t bits = gMinimapTilesWithObtainedItems[a * 32 + row];
            while (bits) {
                if (bits & 1) totalItems++;
                bits >>= 1;
            }
        }
    }
    snprintf(buf, sizeof(buf), "TOTAL ITEMS OBTAINED: %lu", (unsigned long)totalItems);
    DrawText(16.0f, 171.0f, 1.0f, buf, C2D_Color32(180, 240, 160, 255));

    /* RetroAchievements Network & Session Diagnostics */
    char raDiag[80];
    snprintf(raDiag, sizeof(raDiag), "RA: %s (%s)", Port_RA_GetStatusString(GetLang()), Port_RA_GetLastDebugLog());
    DrawText(16.0f, 186.0f, 1.0f, raDiag, C2D_Color32(255, 220, 100, 255));

#ifdef PORT_DEBUG_TOOLS_ACTIVE
    /* [HERRAMIENTAS] button (Y: 198 to 224). Only exists in a DEBUG_TOOLS /
     * *_DIAG_LOG build -- a production build has no way to reach any of the
     * actions behind it, matching how the L+R+<btn> combos are compiled out
     * entirely rather than just hidden (see port_debug_tools.h). */
    DrawButton(16.0f, 198.0f, 288.0f, 26.0f,
        (GetLang() == 6) ? "HERRAMIENTAS DE DEPURACION" : "DEBUG TOOLS",
        C2D_Color32(180, 225, 255, 255), C2D_Color32(30, 55, 90, 255), C2D_Color32(90, 160, 240, 255));

    if (sShowDebugToolsModal) RenderDebugToolsModal(GetLang());
    else if (sShowDebugWarpModal) RenderDebugWarpModal(GetLang());
    else if (sShowDebugEquipModal) RenderDebugEquipModal(GetLang());
    else if (sShowSoundTestModal) RenderSoundTestModal(GetLang());
#else
    DrawText(16.0f, 212.0f, 1.0f, (GetLang() == 6) ? "TOCA LA PESTANA [MAPA] PARA VOLVER" : "TOUCH [MAP] TAB TO RETURN TO MAP VIEW", C2D_Color32(120, 140, 170, 255));
#endif
}

/* ===================================================================== */
/*  ACHIEVEMENTS tab                                                      */
/* ===================================================================== */

/* The list, the pack chooser and the detail popup keep the layout they had
 * as OPTIONS modals; the tab just hosts them. The first look opens the
 * chooser (more than one set) or the flat list. */
static void RenderAchievementsView(void) {
    const int lang = GetLang();
    if (!sShowAchPacksModal && !sShowAchievementsModal) OpenAchievementsUi();
    if (sShowRASettingsModal) {
        RenderRASettingsModal(lang);
    } else if (sShowAchPacksModal) {
        RenderAchPacksModal(lang);
    } else {
        RenderAchievementsModal(lang);
        if (sShowAchDetailModal) RenderAchievementDetailModal(lang);
    }
}

/* ===================================================================== */
/*  ESTADO tab: whole-machine save states (port_save_state.c)             */
/* ===================================================================== */

/* The list is open-ended: a button saves a new state, a tap on a row opens a
 * modal with the state's screenshot and data and the actions that apply to it
 * (load, overwrite, delete). Those three ask for a second tap, armed for
 * STATE_ARM_FRAMES. Everything that touches the SD card is queued in
 * port_save_state.c and shows RenderBusyOverlay while it runs. */
#define STATE_ARM_FRAMES 120
#define STATE_ARM_LOAD      1
#define STATE_ARM_OVERWRITE 2
#define STATE_ARM_DELETE    3
static int      sStateArmAction = 0;
static uint32_t sStateArmFrame  = 0;

#define STATE_NEW_X     16.0f
#define STATE_NEW_Y     30.0f
#define STATE_NEW_W     288.0f
#define STATE_NEW_H     22.0f
#define STATE_LIST_Y0   58.0f
#define STATE_LIST_Y1   206.0f
#define STATE_ROW_PITCH 30.0f
#define STATE_ROW_H     28.0f
#define STATE_TEXT_RIGHT 298.0f   /* right edge of a row's text */

/* Detail modal geometry, shared by the renderer and the touch handler. */
#define SMODAL_X 10.0f
#define SMODAL_Y 26.0f
#define SMODAL_W 300.0f
#define SMODAL_H 206.0f
#define SMODAL_BTN_Y 158.0f
#define SMODAL_BTN_H 22.0f
#define SMODAL_BTN_W 92.0f
static const float kStateBtnX[3] = { 18.0f, 114.0f, 210.0f };   /* load, overwrite, delete */
#define SMODAL_CLOSE_X 116.0f
#define SMODAL_CLOSE_Y 208.0f
#define SMODAL_CLOSE_W 88.0f
#define SMODAL_CLOSE_H 20.0f

static float sStateScrollY = 0.0f;
static PortSaveStateInfo sStateModalInfo;
static unsigned sStateModalNumber;   /* position in the list, from 1: what the player sees */

static bool StateArmed(int action) {
    return sStateArmAction == action && (sFrameCounter - sStateArmFrame) < STATE_ARM_FRAMES;
}

static float StateMaxScroll(void) {
    const float content = (float)Port_SaveState_Count() * STATE_ROW_PITCH;
    const float view = STATE_LIST_Y1 - STATE_LIST_Y0;
    return content > view ? content - view : 0.0f;
}

static void CloseStateModal(void) {
    sStateModalId = 0;
    sStateArmAction = 0;
    PortStateThumb_Set(NULL);
}

static void OpenStateModal(const PortSaveStateInfo* info, unsigned number) {
    sStateModalInfo = *info;
    sStateModalNumber = number;
    sStateModalId = info->id;
    sStateArmAction = 0;
    PortStateThumb_Set(NULL);
    if (info->hasThumb) Port_SaveState_RequestThumb(info->id);
}

/* 12x12 floppy disk centred on (cx, cy): body, metal shutter at the top,
 * label at the bottom. `bg` is the button colour, used for the notch. */
static void DrawFloppyIcon(float cx, float cy, uint32_t ink, uint32_t bg) {
    const float x = floorf(cx) - 6.0f, y = floorf(cy) - 6.0f;
    C2D_DrawRectSolid(x, y, 0.95f, 12.0f, 12.0f, ink);
    C2D_DrawRectSolid(x + 3.0f, y, 0.96f, 6.0f, 4.0f, bg);          /* shutter slot */
    C2D_DrawRectSolid(x + 6.0f, y + 1.0f, 0.97f, 2.0f, 2.0f, ink);  /* shutter pin */
    C2D_DrawRectSolid(x + 2.0f, y + 7.0f, 0.96f, 8.0f, 5.0f, bg);   /* label */
    C2D_DrawRectSolid(x + 3.0f, y + 8.0f, 0.97f, 6.0f, 1.0f, ink);
    C2D_DrawRectSolid(x + 3.0f, y + 10.0f, 0.97f, 6.0f, 1.0f, ink);
}

static void FormatSavedAt(uint32_t savedAt, char* out, size_t n) {
    out[0] = '\0';
    if (savedAt == 0) return;
    time_t t = (time_t)savedAt;   /* the console clock is local time */
    struct tm* tm = gmtime(&t);
    if (tm) snprintf(out, n, "%04d-%02d-%02d %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1,
                     tm->tm_mday, tm->tm_hour, tm->tm_min);
}

/* "E99/99 M5/10 S2/5 P1/3": ammo the save had not found yet (max 0) is left
 * out. Returns the pixel width drawn. */
static void DrawStateStats(float x, float y, const PortSaveStateInfo* info, float clipY0, float clipY1) {
    char seg[20];
    snprintf(seg, sizeof(seg), "E%u/%u", (unsigned)info->energy, (unsigned)info->maxEnergy);
    DrawTextClipped(x, y, 1.0f, seg, C2D_Color32(255, 215, 90, 255), clipY0, clipY1);
    x += ((float)Utf8CharCount(seg) + 1.0f) * 6.0f;
    if (info->maxMissiles > 0) {
        snprintf(seg, sizeof(seg), "M%u/%u", (unsigned)info->missiles, (unsigned)info->maxMissiles);
        DrawTextClipped(x, y, 1.0f, seg, C2D_Color32(255, 130, 110, 255), clipY0, clipY1);
        x += ((float)Utf8CharCount(seg) + 1.0f) * 6.0f;
    }
    if (info->maxSuperMissiles > 0) {
        snprintf(seg, sizeof(seg), "S%u/%u", (unsigned)info->superMissiles, (unsigned)info->maxSuperMissiles);
        DrawTextClipped(x, y, 1.0f, seg, C2D_Color32(120, 220, 130, 255), clipY0, clipY1);
        x += ((float)Utf8CharCount(seg) + 1.0f) * 6.0f;
    }
    if (info->maxPowerBombs > 0) {
        snprintf(seg, sizeof(seg), "P%u/%u", (unsigned)info->powerBombs, (unsigned)info->maxPowerBombs);
        DrawTextClipped(x, y, 1.0f, seg, C2D_Color32(255, 170, 70, 255), clipY0, clipY1);
    }
}

static void RenderStateRow(float y, const PortSaveStateInfo* info, unsigned number, bool es) {
    const float c0 = STATE_LIST_Y0, c1 = STATE_LIST_Y1;
    DrawRectClipped(8.0f, y, 0.5f, 296.0f, STATE_ROW_H, C2D_Color32(14, 22, 34, 255), c0, c1);
    DrawRectClipped(8.0f, y, 0.51f, 296.0f, 1.0f, C2D_Color32(60, 80, 110, 255), c0, c1);

    char head[48];
    snprintf(head, sizeof(head), es ? "#%u  %s  SALA %u" : "#%u  %s  ROOM %u",
             number, Port_SaveState_AreaName(info->area), (unsigned)info->room);
    DrawTextClipped(14.0f, y + 4.0f, 1.0f, head, C2D_Color32(170, 210, 245, 255), c0, c1);

    char when[40];
    FormatSavedAt(info->savedAt, when, sizeof(when));
    if (when[0])
        DrawTextClipped(STATE_TEXT_RIGHT - (float)Utf8CharCount(when) * 6.0f, y + 4.0f, 1.0f, when,
                        C2D_Color32(120, 140, 170, 255), c0, c1);

    if (info->hasStats)
        DrawStateStats(14.0f, y + 15.0f, info, c0, c1);
    else
        DrawTextClipped(14.0f, y + 15.0f, 1.0f, es ? "sin datos (guardado antiguo)" : "no stats (older save)",
                        C2D_Color32(110, 125, 150, 255), c0, c1);

    if (!info->compatible) {
        const char* tag = es ? "OTRA BUILD" : "OTHER BUILD";
        DrawTextClipped(STATE_TEXT_RIGHT - (float)Utf8CharCount(tag) * 6.0f, y + 15.0f, 1.0f, tag,
                        C2D_Color32(235, 110, 90, 255), c0, c1);
    } else if (info->hasPlayTime) {
        char play[16];
        snprintf(play, sizeof(play), "%u:%02u:%02u", (unsigned)info->playHours, (unsigned)info->playMinutes,
                 (unsigned)info->playSeconds);
        DrawTextClipped(STATE_TEXT_RIGHT - (float)Utf8CharCount(play) * 6.0f, y + 15.0f, 1.0f, play,
                        C2D_Color32(150, 170, 200, 255), c0, c1);
    }
}

static void RenderStateModal(bool es, bool avail) {
    const PortSaveStateInfo* info = &sStateModalInfo;
    C2D_DrawRectSolid(SMODAL_X, SMODAL_Y, 0.85f, SMODAL_W, SMODAL_H, C2D_Color32(10, 14, 24, 250));
    C2D_DrawRectSolid(SMODAL_X, SMODAL_Y, 0.84f, SMODAL_W, SMODAL_H, C2D_Color32(40, 70, 120, 255));

    char title[32];
    snprintf(title, sizeof(title), es ? "ESTADO #%u" : "SAVE STATE #%u", sStateModalNumber);
    DrawText(20.0f, 32.0f, 1.0f, title, C2D_Color32(255, 215, 0, 255));

    /* Screenshot, 160x96 inside a frame. */
    C2D_DrawRectSolid(18.0f, 46.0f, 0.86f, 164.0f, 100.0f, C2D_Color32(4, 6, 12, 255));
    if (PortStateThumb_HasImage()) {
        PortStateThumb_Draw(20.0f, 48.0f, 160.0f, 96.0f, 0.9f);
    } else {
        const char* t = info->hasThumb ? "..." : (es ? "SIN IMAGEN" : "NO IMAGE");
        DrawTextCentered(100.0f, 92.0f, 1.0f, t, C2D_Color32(110, 125, 150, 255));
    }

    /* Data. */
    const float tx = 190.0f;
    char line[40];
    snprintf(line, sizeof(line), "%s", Port_SaveState_AreaName(info->area));
    DrawText(tx, 46.0f, 1.0f, line, C2D_Color32(170, 210, 245, 255));
    snprintf(line, sizeof(line), es ? "SALA %u" : "ROOM %u", (unsigned)info->room);
    DrawText(tx, 56.0f, 1.0f, line, C2D_Color32(170, 210, 245, 255));
    if (info->savedAt) {
        char when[40];
        FormatSavedAt(info->savedAt, when, sizeof(when));
        char* sp = strchr(when, ' ');
        if (sp) {
            *sp = '\0';
            DrawText(tx, 69.0f, 1.0f, when, C2D_Color32(120, 140, 170, 255));
            DrawText(tx, 79.0f, 1.0f, sp + 1, C2D_Color32(120, 140, 170, 255));
        }
    }
    if (info->hasPlayTime) {
        snprintf(line, sizeof(line), es ? "JUEGO %u:%02u:%02u" : "TIME %u:%02u:%02u", (unsigned)info->playHours,
                 (unsigned)info->playMinutes, (unsigned)info->playSeconds);
        DrawText(tx, 92.0f, 1.0f, line, C2D_Color32(150, 170, 200, 255));
    }
    if (info->hasStats) {
        snprintf(line, sizeof(line), "E %u/%u", (unsigned)info->energy, (unsigned)info->maxEnergy);
        DrawText(tx, 106.0f, 1.0f, line, C2D_Color32(255, 215, 90, 255));
        if (info->maxMissiles > 0) {
            snprintf(line, sizeof(line), "M %u/%u", (unsigned)info->missiles, (unsigned)info->maxMissiles);
            DrawText(tx, 116.0f, 1.0f, line, C2D_Color32(255, 130, 110, 255));
        }
        if (info->maxSuperMissiles > 0) {
            snprintf(line, sizeof(line), "S %u/%u", (unsigned)info->superMissiles, (unsigned)info->maxSuperMissiles);
            DrawText(tx, 126.0f, 1.0f, line, C2D_Color32(120, 220, 130, 255));
        }
        if (info->maxPowerBombs > 0) {
            snprintf(line, sizeof(line), "P %u/%u", (unsigned)info->powerBombs, (unsigned)info->maxPowerBombs);
            DrawText(tx, 136.0f, 1.0f, line, C2D_Color32(255, 170, 70, 255));
        }
    } else {
        DrawText(tx, 106.0f, 1.0f, es ? "SIN DATOS" : "NO STATS", C2D_Color32(110, 125, 150, 255));
    }

    /* Actions. */
    const bool canLoad = info->compatible;   /* loading works anywhere; saving needs gameplay */
    const bool canOverwrite = avail;
    static const char* const labelsEs[3] = { "CARGAR", "SOBRESCRIBIR", "BORRAR" };
    static const char* const labelsEn[3] = { "LOAD", "OVERWRITE", "DELETE" };
    const char* const* labels = es ? labelsEs : labelsEn;
    const bool enabled[3] = { canLoad, canOverwrite, true };
    const int actions[3] = { STATE_ARM_LOAD, STATE_ARM_OVERWRITE, STATE_ARM_DELETE };
    for (int i = 0; i < 3; ++i) {
        const bool armed = enabled[i] && StateArmed(actions[i]);
        uint32_t body, edge, ink;
        if (!enabled[i]) {
            body = C2D_Color32(30, 34, 40, 255); edge = C2D_Color32(60, 66, 78, 255); ink = C2D_Color32(90, 100, 115, 255);
        } else if (armed) {
            body = C2D_Color32(120, 90, 20, 255); edge = C2D_Color32(220, 180, 70, 255); ink = C2D_Color32(255, 235, 150, 255);
        } else if (i == 2) {
            body = C2D_Color32(64, 22, 22, 255); edge = C2D_Color32(180, 60, 60, 255); ink = C2D_Color32(255, 170, 170, 255);
        } else if (i == 1) {
            body = C2D_Color32(24, 60, 34, 255); edge = C2D_Color32(70, 150, 90, 255); ink = C2D_Color32(200, 240, 205, 255);
        } else {
            body = C2D_Color32(24, 46, 70, 255); edge = C2D_Color32(80, 140, 200, 255); ink = C2D_Color32(200, 225, 245, 255);
        }
        DrawButton(kStateBtnX[i], SMODAL_BTN_Y, SMODAL_BTN_W, SMODAL_BTN_H, armed ? "OK?" : labels[i], ink, body, edge);
    }

    const char* hint = NULL;
    if (!info->compatible) hint = es ? "GUARDADO CON OTRA BUILD: NO SE PUEDE CARGAR" : "SAVED BY ANOTHER BUILD: CANNOT LOAD";
    else if (!avail) hint = es ? "SOBRESCRIBIR: SOLO EN PARTIDA" : "OVERWRITE: IN GAMEPLAY ONLY";
    else hint = es ? "PULSA DOS VECES PARA CONFIRMAR" : "TAP TWICE TO CONFIRM";
    DrawTextCentered(160.0f, 188.0f, 1.0f, hint,
                     info->compatible ? C2D_Color32(120, 140, 170, 255) : C2D_Color32(235, 110, 90, 255));

    DrawButton(SMODAL_CLOSE_X, SMODAL_CLOSE_Y, SMODAL_CLOSE_W, SMODAL_CLOSE_H, es ? "VOLVER" : "BACK",
               C2D_Color32(255, 255, 255, 255), BTN_BLUE_BODY, BTN_BLUE_BORDER);
}

static void RenderStateView(void) {
    const bool es = (GetLang() == 6);
    const bool avail = Port_SaveState_Available();

    if (sStateModalId) {
        RenderStateModal(es, avail);
        return;
    }

    /* New state. */
    const uint32_t newBody = avail ? C2D_Color32(24, 60, 34, 255) : C2D_Color32(30, 34, 40, 255);
    const uint32_t newInk = avail ? C2D_Color32(200, 240, 205, 255) : C2D_Color32(90, 100, 115, 255);
    DrawButton(STATE_NEW_X, STATE_NEW_Y, STATE_NEW_W, STATE_NEW_H, es ? "NUEVO GUARDADO" : "NEW SAVE STATE",
               newInk, newBody, avail ? C2D_Color32(70, 150, 90, 255) : C2D_Color32(60, 66, 78, 255));
    DrawFloppyIcon(STATE_NEW_X + 16.0f, STATE_NEW_Y + STATE_NEW_H * 0.5f, newInk, newBody);

    /* The list, newest first. */
    const int count = Port_SaveState_Count();
    const float maxScroll = StateMaxScroll();
    if (sStateScrollY > maxScroll) sStateScrollY = maxScroll;
    if (sStateScrollY < 0.0f) sStateScrollY = 0.0f;

    if (count == 0) {
        DrawTextCentered(160.0f, 120.0f, 1.0f,
                         Port_SaveState_Scanned() ? (es ? "NO HAY ESTADOS GUARDADOS" : "NO SAVE STATES YET") : "",
                         C2D_Color32(110, 125, 150, 255));
    }
    for (int i = 0; i < count; ++i) {
        const float y = STATE_LIST_Y0 + (float)i * STATE_ROW_PITCH - sStateScrollY;
        if (y + STATE_ROW_H <= STATE_LIST_Y0) continue;
        if (y >= STATE_LIST_Y1) break;
        PortSaveStateInfo info;
        if (Port_SaveState_GetInfoAt(i, &info)) RenderStateRow(y, &info, (unsigned)i + 1u, es);
    }
    if (maxScroll > 0.0f) {
        const float trackH = STATE_LIST_Y1 - STATE_LIST_Y0;
        const float thumbH = 24.0f;
        const float thumbY = STATE_LIST_Y0 + (sStateScrollY / maxScroll) * (trackH - thumbH);
        C2D_DrawRectSolid(310.0f, STATE_LIST_Y0, 0.9f, 2.0f, trackH, C2D_Color32(40, 50, 70, 255));
        C2D_DrawRectSolid(310.0f, thumbY, 0.92f, 2.0f, thumbH, C2D_Color32(80, 160, 240, 255));
    }

    const char* msg = Port_SaveState_LastMessage();
    if (msg && msg[0] && Port_SaveState_MessageTtl() > 0) {
        DrawTextCentered(160.0f, 216.0f, 1.0f, msg, C2D_Color32(255, 235, 150, 255));
    } else if (!avail) {
        DrawTextCentered(160.0f, 216.0f, 1.0f, es ? "GUARDAR: SOLO DURANTE LA PARTIDA" : "SAVING: ONLY DURING GAMEPLAY",
                         C2D_Color32(150, 165, 190, 255));
    } else {
        DrawTextCentered(160.0f, 216.0f, 1.0f, es ? "TOCA UN ESTADO PARA VER SUS DATOS" : "TAP A STATE FOR ITS DETAILS",
                         C2D_Color32(120, 140, 170, 255));
    }
}

static bool StateInRect(int x, int y, float rx, float ry, float rw, float rh) {
    return (float)x >= rx && (float)x <= rx + rw && (float)y >= ry && (float)y <= ry + rh;
}

static void HandleStateModalTouch(int x, int y) {
    const bool avail = Port_SaveState_Available();
    const PortSaveStateInfo* info = &sStateModalInfo;

    if (StateInRect(x, y, SMODAL_CLOSE_X, SMODAL_CLOSE_Y, SMODAL_CLOSE_W, SMODAL_CLOSE_H)) {
        CloseStateModal();
        return;
    }

    const bool enabled[3] = { info->compatible, avail, true };
    const int actions[3] = { STATE_ARM_LOAD, STATE_ARM_OVERWRITE, STATE_ARM_DELETE };
    for (int i = 0; i < 3; ++i) {
        if (!StateInRect(x, y, kStateBtnX[i], SMODAL_BTN_Y, SMODAL_BTN_W, SMODAL_BTN_H)) continue;
        if (!enabled[i]) return;
        if (!StateArmed(actions[i])) {
            sStateArmAction = actions[i];
            sStateArmFrame = sFrameCounter;
            return;
        }
        const uint32_t id = info->id;
        CloseStateModal();
        if (actions[i] == STATE_ARM_LOAD) Port_SaveState_RequestLoad(id);
        else if (actions[i] == STATE_ARM_OVERWRITE) Port_SaveState_RequestOverwrite(id);
        else Port_SaveState_RequestDelete(id);
        return;
    }
    sStateArmAction = 0;   /* a tap anywhere else disarms */
}

static void HandleStateTouch(int x, int y, bool isNewTap) {
    if (sStateModalId) {
        if (isNewTap) HandleStateModalTouch(x, y);
        Port_BottomUI_MarkDirty();
        return;
    }

    if (isNewTap) {
        if (StateInRect(x, y, STATE_NEW_X, STATE_NEW_Y, STATE_NEW_W, STATE_NEW_H)) {
            if (Port_SaveState_Available()) Port_SaveState_RequestSaveNew();
            return;
        }
        if ((float)y >= STATE_LIST_Y0 && (float)y < STATE_LIST_Y1) {
            sTouchStartX = x;
            sTouchStartY = y;
            sLastTouchX = x;
            sLastTouchY = y;
            sIsTouchDragging = false;
        }
        return;
    }

    /* Drag the list. A press wobbles a few pixels before it lifts, so
     * scrolling only starts once the finger has travelled past a small
     * threshold; below it the release still counts as a tap on the row. */
    if (sLastTouchY < 0 || sTouchStartY < 0) return;
    const float DRAG_SLOP = 6.0f;
    const float total = (float)(y - sTouchStartY);
    if (!sIsTouchDragging && total > -DRAG_SLOP && total < DRAG_SLOP) {
        sLastTouchX = x;
        sLastTouchY = y;
        return;
    }
    sStateScrollY += (float)(sLastTouchY - y);
    const float maxScroll = StateMaxScroll();
    if (sStateScrollY < 0.0f) sStateScrollY = 0.0f;
    if (sStateScrollY > maxScroll) sStateScrollY = maxScroll;
    sLastTouchX = x;
    sLastTouchY = y;
    sIsTouchDragging = true;
}

/* A release that never became a drag opens the row it started on. */
static void StateTapReleased(void) {
    if (sStateModalId || Port_SaveState_IsBusy()) return;
    if (sIsTouchDragging || sTouchStartY < 0) return;
    if ((float)sTouchStartY < STATE_LIST_Y0 || (float)sTouchStartY >= STATE_LIST_Y1) return;
    const float local = (float)sTouchStartY - STATE_LIST_Y0 + sStateScrollY;
    const int idx = (int)(local / STATE_ROW_PITCH);
    if (local - (float)idx * STATE_ROW_PITCH >= STATE_ROW_H) return;   /* the gap between rows */
    PortSaveStateInfo info;
    if (Port_SaveState_GetInfoAt(idx, &info)) {
        OpenStateModal(&info, (unsigned)idx + 1u);
        Port_BottomUI_MarkDirty();
    }
}

static void RenderDevFtp(void) {
    if (!FtpServer_InstallOnly()) return;
    char address[48];
    FtpServer_Address(address, sizeof(address));
    C2D_DrawRectSolid(0.0f, 200.0f, 0.97f, 320.0f, 40.0f, C2D_Color32(10, 14, 24, 255));
    C2D_DrawRectSolid(0.0f, 200.0f, 0.97f, 320.0f, 1.0f, C2D_Color32(245, 175, 45, 255));
    DrawText(8.0f, 206.0f, 1.0f, "FTP (INSTALL ONLY)", C2D_Color32(255, 215, 0, 255));
    DrawText(8.0f, 222.0f, 1.0f, address, C2D_Color32(190, 210, 240, 255));
    C2D_DrawRectSolid(DEVFTP_X, DEVFTP_Y, 0.97f, DEVFTP_W, DEVFTP_H, C2D_Color32(120, 150, 210, 255));
    C2D_DrawRectSolid(DEVFTP_X + 1, DEVFTP_Y + 1, 0.97f, DEVFTP_W - 2, DEVFTP_H - 2, C2D_Color32(30, 40, 70, 255));
    DrawText(DEVFTP_X + 17.0f, DEVFTP_Y + 10.0f, 1.0f, "CANCEL", C2D_Color32(255, 215, 0, 255));
}

/* "Wait" message over whatever tab is showing while the SD card is read or
 * written. The thumbnail read is too short to deserve it. */
/* The notice raised by Port_BottomUI_ShowNotice: the same frame and text
 * colours as the rest of the bottom screen's dialogs, an amber warning
 * accent, centred and floating over whatever tab is showing. */
static void RenderNoticeToast(void) {
    const char* standing = StandingNotice();
    /* A standing notice is drawn steady (no slide, no fade) for as long as it lasts. */
    const int elapsed = standing ? (int)NOTICE_ENTER_MS : NoticeElapsedMs();
    if (elapsed < 0) return;
    const char* title = standing ? "NEW BUILD" : sNoticeTitle;
    const char* body = standing ? standing : sNoticeBody;

    float alpha = 1.0f;
    float rise = 0.0f;
    if (elapsed < (int)NOTICE_ENTER_MS) {
        const float t = (float)elapsed / (float)NOTICE_ENTER_MS;
        const float ease = 1.0f - (1.0f - t) * (1.0f - t); /* ease-out */
        alpha = ease;
        rise = (1.0f - ease) * 10.0f;
    } else if (elapsed >= (int)(NOTICE_SHOW_MS - NOTICE_FADE_MS)) {
        alpha = (float)(NOTICE_SHOW_MS - (unsigned)elapsed) / (float)NOTICE_FADE_MS;
    }
#define NOTICE_RGBA(r, g, b, a) C2D_Color32((r), (g), (b), (uint8_t)((float)(a) * alpha))

    const float icon = 22.0f;
    const float padX = 10.0f;
    const float gap = 10.0f;
    const float titleW = (float)Utf8CharCount(title) * 6.0f - 1.0f;
    const float bodyW = (float)Utf8CharCount(body) * 6.0f - 1.0f;
    const float textW = titleW > bodyW ? titleW : bodyW;
    const float boxW = (float)(int)(4.0f + padX + icon + gap + textW + padX);
    const float boxH = 44.0f;
    const float x = (float)(int)((320.0f - boxW) / 2.0f);
    const float y = (float)(int)((240.0f - boxH) / 2.0f + rise);

    C2D_DrawRectSolid(x + 3.0f, y + 3.0f, 0.97f, boxW, boxH, NOTICE_RGBA(0, 0, 0, 110));
    C2D_DrawRectSolid(x, y, 0.97f, boxW, boxH, NOTICE_RGBA(120, 150, 210, 255));
    C2D_DrawRectSolid(x + 1.0f, y + 1.0f, 0.97f, boxW - 2.0f, boxH - 2.0f, NOTICE_RGBA(10, 14, 24, 245));
    C2D_DrawRectSolid(x + 1.0f, y + 1.0f, 0.97f, 3.0f, boxH - 2.0f, NOTICE_RGBA(245, 175, 45, 255));

    /* Warning badge: an amber tile with clipped corners and a "!". */
    const float ix = x + 4.0f + padX;
    const float iy = y + (boxH - icon) / 2.0f;
    C2D_DrawRectSolid(ix + 1.0f, iy, 0.97f, icon - 2.0f, icon, NOTICE_RGBA(245, 175, 45, 255));
    C2D_DrawRectSolid(ix, iy + 1.0f, 0.97f, icon, icon - 2.0f, NOTICE_RGBA(245, 175, 45, 255));
    DrawText(ix + (icon - 10.0f) / 2.0f, iy + (icon - 14.0f) / 2.0f, 2.0f, "!", NOTICE_RGBA(10, 14, 24, 255));

    const float tx = ix + icon + gap;
    DrawText(tx, y + 10.0f, 1.0f, title, NOTICE_RGBA(255, 215, 0, 255));
    DrawText(tx, y + 26.0f, 1.0f, body, NOTICE_RGBA(190, 210, 240, 255));
#undef NOTICE_RGBA
}

void Port_NesMetroid_ShowUnavailable(void) {
    if (GetLang() == 6) {
        Port_BottomUI_ShowNotice("NO DISPONIBLE", "METROID DE NES NO ESTÁ EN ESTE PORT");
    } else {
        Port_BottomUI_ShowNotice("NOT AVAILABLE", "NES METROID IS NOT IN THIS PORT");
    }
}

void Port_FusionLink_ShowUnavailable(void) {
    if (GetLang() == 6) {
        Port_BottomUI_ShowNotice("NO DISPONIBLE", "LA CONEXIÓN CON FUSION PIDE CABLE LINK");
    } else {
        Port_BottomUI_ShowNotice("NOT AVAILABLE", "FUSION LINK NEEDS A GBA LINK CABLE");
    }
}

/* The game's save file (mzm.sav) could not be written (src/sram/sram.c). */
void Port_SaveFile_ShowFailed(void) {
    if (GetLang() == 6) {
        Port_BottomUI_ShowNotice("ERROR AL GUARDAR", "NO SE PUDO ESCRIBIR LA PARTIDA");
    } else {
        Port_BottomUI_ShowNotice("SAVE FAILED", "THE SAVE FILE COULD NOT BE WRITTEN");
    }
}

static void RenderBusyOverlay(void) {
    const PortSaveStateJob job = Port_SaveState_CurrentJob();
    if (job == PORT_SS_JOB_NONE || job == PORT_SS_JOB_THUMB) return;
    const bool es = (GetLang() == 6);
    const char* what = "";
    switch (job) {
    case PORT_SS_JOB_SCAN:      what = es ? "LEYENDO LA TARJETA SD" : "READING THE SD CARD"; break;
    case PORT_SS_JOB_SAVE:
    case PORT_SS_JOB_OVERWRITE: what = es ? "GUARDANDO EN LA TARJETA SD" : "SAVING TO THE SD CARD"; break;
    case PORT_SS_JOB_LOAD:      what = es ? "CARGANDO DESDE LA TARJETA SD" : "LOADING FROM THE SD CARD"; break;
    case PORT_SS_JOB_DELETE:    what = es ? "BORRANDO DE LA TARJETA SD" : "DELETING FROM THE SD CARD"; break;
    default: break;
    }
    C2D_DrawRectSolid(0.0f, 0.0f, 0.985f, 320.0f, 240.0f, C2D_Color32(0, 0, 0, 150));
    C2D_DrawRectSolid(30.0f, 84.0f, 0.99f, 260.0f, 72.0f, C2D_Color32(120, 150, 210, 255));
    C2D_DrawRectSolid(31.0f, 85.0f, 0.995f, 258.0f, 70.0f, C2D_Color32(10, 14, 24, 255));
    DrawTextCentered(160.0f, 104.0f, 1.0f, es ? "ESPERA..." : "PLEASE WAIT...", C2D_Color32(255, 215, 0, 255));
    DrawTextCentered(160.0f, 124.0f, 1.0f, what, C2D_Color32(190, 210, 240, 255));
}

void Port_BottomUI_Render(void) {
    /* sFrameCounter is advanced by Port_BottomUI_FrameTick every frame, not
     * here -- this function is throttled (see Port_BottomUI_WantsRedraw). */

    /* Background clear for UI — blinks during low health to catch attention.
     * Blink pattern: 14 frames ON (warning color), 14 frames dimmed.
     * The entire screen background participates so even a glance shows it. */
    {
        bool inRealGameplay = ((uint8_t)gMainGameMode == 4 /* GM_INGAME */);
        uint16_t curE = gEquipment.currentEnergy;
        bool blinkOn = ((sFrameCounter & 0x0F) < 8);
        uint32_t bgCol;
        if (inRealGameplay && curE < 30) {
            bgCol = blinkOn ? C2D_Color32(30, 8, 8, 255)
                            : C2D_Color32(12, 5, 5, 255);
        } else if (inRealGameplay && curE < 60) {
            bgCol = blinkOn ? C2D_Color32(30, 22, 8, 255)
                            : C2D_Color32(12, 10, 5, 255);
        } else {
            bgCol = C2D_Color32(6, 8, 14, 255);       /* original dark blue */
        }
        C2D_DrawRectSolid(0.0f, 0.0f, 0.1f, 320.0f, 240.0f, bgCol);
    }

    /* Always draw the top tab navigation bar */
    RenderTabBar();

    /* Render active view */
    switch (sCurrentTab) {
        case BOTTOM_TAB_MAP:
            RenderMapView();
            break;
        case BOTTOM_TAB_STATUS:
            RenderStatusView();
            break;
        case BOTTOM_TAB_DEBUG:
            RenderDebugView();
            break;
        case BOTTOM_TAB_OPTIONS:
            RenderOptionsView();
            break;
        case BOTTOM_TAB_STATE:
            RenderStateView();
            break;
        case BOTTOM_TAB_ACHIEVEMENTS:
            RenderAchievementsView();
            break;
        default:
            RenderMapView();
            break;
    }

    /* RA session pump runs every frame in Port_BottomUI_FrameTick; here we
     * only draw the toast (if one is active) onto this frame's target. */
    Port_RA_RenderToastOverlay();
    RenderUpdatePrompt(GetLang());
    RenderBusyOverlay();
    RenderNoticeToast();
    RenderDevFtp();

    /* L+R+START scene recorder indicator (platform_gpu_3ds.c) -- drawn last,
     * on top of whichever tab is active, so it's never hidden by one. Blinks
     * so it's noticeable even glanced at briefly mid-gameplay. Uses the same
     * border+fill panel style as the rest of the bottom UI so it doesn't
     * disappear against busy background art. */
    extern bool PlatformGpu3DS_IsRecording(void);
    extern bool PlatformGpu3DS_IsPerfRecording(void);
    extern uint32_t PlatformGpu3DS_LastRecTickUs(void);
    const bool blink = (sFrameCounter & 0x20) != 0;
    /* 10x10 colored square + 5x7 bitmap text (7px tall). Text vertically
     * centered in the square: offset = (10-7)/2 = 1px. Panel wraps the
     * content with 3px border + 2px fill padding on all sides. */
    if (blink && (PlatformGpu3DS_IsRecording() || PlatformGpu3DS_IsPerfRecording())) {
        C2D_DrawRectSolid(5.0f, 5.0f, 0.90f, 62.0f, 40.0f, C2D_Color32(40, 70, 120, 255));
        C2D_DrawRectSolid(6.0f, 6.0f, 0.91f, 60.0f, 38.0f, C2D_Color32(14, 20, 32, 240));
    }
    if (PlatformGpu3DS_IsRecording() && blink) {
        C2D_DrawRectSolid(8.0f, 8.0f, 0.95f, 10.0f, 10.0f, C2D_Color32(230, 30, 30, 255));
        DrawText(24.0f, 9.0f, 0.95f, "REC", C2D_Color32(230, 30, 30, 255));
        /* Cost of the recorder's own per-sample work. Reading this ~= a
         * whole frame budget means the crawl IS the recorder. */
        extern bool PlatformGpu3DS_RecordingToRam(void);
        char dt[28];
        snprintf(dt, sizeof(dt), "%s %lu us",
                 PlatformGpu3DS_RecordingToRam() ? "RAM" : "SD",
                 (unsigned long)PlatformGpu3DS_LastRecTickUs());
        DrawText(9.0f, 32.0f, 0.95f, dt, C2D_Color32(210, 210, 210, 255));
    }
    if (PlatformGpu3DS_IsPerfRecording() && blink) {
        C2D_DrawRectSolid(8.0f, 20.0f, 0.95f, 10.0f, 10.0f, C2D_Color32(30, 120, 230, 255));
        DrawText(24.0f, 21.0f, 0.95f, "PERF", C2D_Color32(30, 120, 230, 255));
    }
}
