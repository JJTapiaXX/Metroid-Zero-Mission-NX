#if defined(PORT_DEBUG_TOOLS)
#include "debug_link_3ds.h"

#include "platform_gpu_3ds.h"
#include "port_save_state.h"

#include <3ds.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The game's own types clash with <3ds.h>, so what reads them lives in port_save_state.c. */
extern void Port_DebugLink_StateLine(char* out, unsigned cap);
extern void Port_DebugLog(const char* msg);
extern void Port_BottomUI_HandleTouchDrag(int x, int y, bool isNewTap);
extern void Port_BottomUI_TouchReleased(void);

#define LINK_DEBUG_DIR "sdmc:/3ds/Metroid Zero Mission 3DS/debug"

/* A request the logic thread never takes (game hung or in a stalled transition). */
#define LINK_WAIT_NS 20000000000LL

enum {
    LINK_PEEK_MAX = 256,
    LINK_RESULT_MAX = 600,
};

typedef enum LinkOp {
    LINK_OP_NONE,
    LINK_OP_TOUCH,
    LINK_OP_SHOT,
    LINK_OP_DUMP,
    LINK_OP_STATE_SAVE,
    LINK_OP_STATE_LOAD,
    LINK_OP_STATE_DELETE,
    LINK_OP_STATES,
    LINK_OP_SWITCH,
    LINK_OP_STATE_LINE,
    LINK_OP_POKE,
} LinkOp;

/* One request at a time: the FTP server serves one client and one command. */
static LightLock sLock = 1; /* LightLock_Init's value, so no init call is needed */
static LightEvent sDone;
static bool sDoneReady;
static volatile LinkOp sOp;
static LinkOp sActive; /* a request that takes more than one frame */
static unsigned sActiveWaited;
static int sArgA, sArgB;
static uintptr_t sPokeAddress;
static uint8_t sPokeBytes[LINK_PEEK_MAX];
static unsigned sPokeLength;
static char sResult[LINK_RESULT_MAX];
static uint64_t sFrame;
static uint32_t sStateId; /* the id a save state job works on */
static char sStateMessage[48];

/* Injected buttons: written by the FTP thread, read by the logic thread. */
static volatile uint32_t sKeysMask;
static volatile uint32_t sKeysFrames;
static volatile bool sKeysNew;
static uint64_t sKeysUntil;
static uint32_t sKeysNow; /* what is injected as held right now */

static const struct {
    const char* name;
    uint32_t mask;
} kKeys[] = {
    { "A", KEY_A },         { "B", KEY_B },           { "X", KEY_X },        { "Y", KEY_Y },
    { "L", KEY_L },         { "R", KEY_R },           { "ZL", KEY_ZL },      { "ZR", KEY_ZR },
    { "START", KEY_START }, { "SELECT", KEY_SELECT }, { "UP", KEY_DUP },     { "DOWN", KEY_DDOWN },
    { "LEFT", KEY_DLEFT },  { "RIGHT", KEY_DRIGHT },
};

static bool ParseKeys(const char* text, uint32_t* mask) {
    *mask = 0;
    char word[16];
    while (*text && *text != ' ') {
        unsigned n = 0;
        while (*text && *text != '+' && *text != ' ' && n + 1 < sizeof(word)) word[n++] = (char)toupper((unsigned char)*text++);
        word[n] = 0;
        if (*text == '+') ++text;
        if (!strcmp(word, "NONE")) continue;
        bool found = false;
        for (unsigned i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); ++i) {
            if (!strcmp(word, kKeys[i].name)) {
                *mask |= kKeys[i].mask;
                found = true;
            }
        }
        if (!found) return false;
    }
    return true;
}

/* Readable (or writable) for `length` bytes, page by page: a bad address from the PC must not
 * crash the game. */
static bool MemoryOk(uintptr_t address, unsigned length, bool write) {
    uintptr_t at = address;
    const uintptr_t end = address + length;
    while (at < end) {
        MemInfo info;
        PageInfo page;
        if (R_FAILED(svcQueryMemory(&info, &page, (u32)at))) return false;
        if (info.state == MEMSTATE_FREE) return false;
        if (!(info.perm & (write ? MEMPERM_WRITE : MEMPERM_READ))) return false;
        at = info.base_addr + info.size;
    }
    return true;
}

static int ParseHex(const char* text, uint8_t* out, unsigned cap) {
    unsigned n = 0;
    while (text[0] && text[1] && n < cap) {
        char pair[3] = { text[0], text[1], 0 };
        char* end;
        long v = strtol(pair, &end, 16);
        if (*end) return -1;
        out[n++] = (uint8_t)v;
        text += 2;
    }
    return text[0] ? -1 : (int)n;
}

/* Hands the request to the logic thread and waits for its result line. */
static void RunOnLogicThread(LinkOp op, char* reply, unsigned cap) {
    LightLock_Lock(&sLock);
    if (!sDoneReady) {
        LightEvent_Init(&sDone, RESET_ONESHOT);
        sDoneReady = true;
    }
    LightEvent_Clear(&sDone);
    sResult[0] = 0;
    __atomic_store_n(&sOp, op, __ATOMIC_RELEASE);
    if (LightEvent_WaitTimeout(&sDone, LINK_WAIT_NS) != 0) {
        __atomic_store_n(&sOp, LINK_OP_NONE, __ATOMIC_RELEASE);
        snprintf(reply, cap, "451 The game did not take the request (stopped?).");
    } else {
        snprintf(reply, cap, "%s", sResult);
    }
    LightLock_Unlock(&sLock);
}

/* ---- SWITCH: the DEBUG -> HERRAMIENTAS values that flip --------------------------------------- */

extern bool Port_GpuRenderer_IsActive(void);
extern void Port_GpuRenderer_SetActive(bool on);
extern bool Port_GpuRenderer_DepthTintEnabled(void);
extern void Port_GpuRenderer_SetDepthTint(bool on);
extern bool Port_Bios_AdaptiveFrameSkipEnabled(void);
extern void Port_Bios_SetAdaptiveFrameSkip(bool on);
extern bool Platform3DS_HardwareIsNew3DS(void);
extern bool Platform3DS_ForcedOld3DSProfile(void);
extern void Platform3DS_SetForcedOld3DSProfile(bool forced);
extern bool Port_DebugLog_IsBuffered(void);
extern void Port_DebugLog_SetBuffered(bool buffered);

static const char* const kSwitchNames[] = { "RENDERER", "FRAME SKIP", "DEPTH TINT", "OLD 3DS PROFILE", "LOG BUFFERING" };
enum { kSwitchCount = sizeof(kSwitchNames) / sizeof(kSwitchNames[0]) };

static bool NameMatches(const char* name, const char* arg) {
    /* "FRAME SKIP" matches FRAME_SKIP, frame-skip and frame skip. */
    unsigned i = 0;
    while (name[i] && arg[i] &&
           (toupper((unsigned char)arg[i]) == name[i] || (name[i] == ' ' && (arg[i] == '_' || arg[i] == '-'))))
        ++i;
    return !name[i] && !arg[i];
}

/* Flips switch `id`; writes "NAME = value" (or why not) into sResult. */
static void FlipSwitch(int id) {
    const char* value = "?";
    switch (id) {
        case 0: {
            const bool on = !Port_GpuRenderer_IsActive();
            Port_GpuRenderer_SetActive(on);
            value = on ? "GPU" : "CPU";
            break;
        }
        case 1: {
            const bool on = !Port_Bios_AdaptiveFrameSkipEnabled();
            Port_Bios_SetAdaptiveFrameSkip(on);
            value = on ? "ON" : "OFF";
            break;
        }
        case 2: {
            const bool on = !Port_GpuRenderer_DepthTintEnabled();
            Port_GpuRenderer_SetDepthTint(on);
            value = on ? "ON" : "OFF";
            break;
        }
        case 3: {
            if (!Platform3DS_HardwareIsNew3DS()) {
                snprintf(sResult, sizeof(sResult), "550 OLD 3DS PROFILE: this is an Old 3DS, nothing to force.");
                return;
            }
            const bool forced = !Platform3DS_ForcedOld3DSProfile();
            Platform3DS_SetForcedOld3DSProfile(forced);
            value = forced ? "OLD (forced)" : "NEW";
            break;
        }
        case 4: {
            const bool on = !Port_DebugLog_IsBuffered();
            Port_DebugLog_SetBuffered(on);
            value = on ? "ON" : "OFF";
            break;
        }
        default: break;
    }
    char line[96];
    snprintf(line, sizeof(line), "switch %s -> %s (link)", kSwitchNames[id], value);
    Port_DebugLog(line);
    snprintf(sResult, sizeof(sResult), "200 %s = %s", kSwitchNames[id], value);
}

/* ---- the FTP thread's side ------------------------------------------------------------------ */

static void SetStateOp(const char* verb, const char* args, char* reply, unsigned cap) {
    const long n = strtol(args, NULL, 10);
    if (!strcmp(verb, "DELSTATE") && n <= 0) {
        snprintf(reply, cap, "501 DELSTATE <number from SITE STATES>");
        return;
    }
    sArgA = (int)n; /* 0: the default (a new state for SAVESTATE, the newest otherwise) */
    RunOnLogicThread(!strcmp(verb, "SAVESTATE") ? LINK_OP_STATE_SAVE
                     : !strcmp(verb, "LOADSTATE") ? LINK_OP_STATE_LOAD
                                                  : LINK_OP_STATE_DELETE,
                     reply, cap);
}

bool DebugLink_Command(const char* verb, const char* args, char* reply, unsigned cap) {
    if (!args) args = "";
    if (!strcmp(verb, "KEYS")) {
        uint32_t mask;
        if (!ParseKeys(args, &mask)) {
            snprintf(reply, cap, "501 KEYS <A+B+LEFT...|NONE> [frames]");
            return true;
        }
        const char* frames = strchr(args, ' ');
        long n = frames ? strtol(frames + 1, NULL, 10) : 1;
        if (n < 1) n = 1;
        sKeysMask = mask;
        sKeysFrames = (uint32_t)n;
        __atomic_store_n(&sKeysNew, true, __ATOMIC_RELEASE);
        snprintf(reply, cap, "200 Holding %s for %ld frames.", args, n);
        return true;
    }
    if (!strcmp(verb, "TOUCH")) {
        if (sscanf(args, "%d %d", &sArgA, &sArgB) != 2 || sArgA < 0 || sArgA >= 320 || sArgB < 0 || sArgB >= 240) {
            snprintf(reply, cap, "501 TOUCH <x 0-319> <y 0-239>");
            return true;
        }
        RunOnLogicThread(LINK_OP_TOUCH, reply, cap);
        return true;
    }
    if (!strcmp(verb, "SHOT")) {
        RunOnLogicThread(LINK_OP_SHOT, reply, cap);
        return true;
    }
    if (!strcmp(verb, "DUMP")) {
        RunOnLogicThread(LINK_OP_DUMP, reply, cap);
        return true;
    }
    if (!strcmp(verb, "STATE")) {
        RunOnLogicThread(LINK_OP_STATE_LINE, reply, cap);
        return true;
    }
    if (!strcmp(verb, "STATES")) {
        RunOnLogicThread(LINK_OP_STATES, reply, cap);
        return true;
    }
    if (!strcmp(verb, "SAVESTATE") || !strcmp(verb, "LOADSTATE") || !strcmp(verb, "DELSTATE")) {
        SetStateOp(verb, args, reply, cap);
        return true;
    }
    if (!strcmp(verb, "SWITCH")) {
        sArgA = -1;
        for (int i = 0; i < kSwitchCount; ++i)
            if (NameMatches(kSwitchNames[i], args)) sArgA = i;
        if (sArgA < 0) {
            int n = snprintf(reply, cap, "501 SWITCH <name>: ");
            for (int i = 0; i < kSwitchCount && (unsigned)n + 20 < cap; ++i)
                n += snprintf(reply + n, cap - (unsigned)n, "%s%s", i ? ", " : "", kSwitchNames[i]);
            return true;
        }
        RunOnLogicThread(LINK_OP_SWITCH, reply, cap);
        return true;
    }
    if (!strcmp(verb, "PEEK")) {
        unsigned long address = 0, length = 0;
        if (sscanf(args, "%lx %lu", &address, &length) != 2 || length == 0 || length > LINK_PEEK_MAX) {
            snprintf(reply, cap, "501 PEEK <hex address> <length 1-%d>", LINK_PEEK_MAX);
            return true;
        }
        if (!MemoryOk((uintptr_t)address, (unsigned)length, false)) {
            snprintf(reply, cap, "550 Not readable.");
            return true;
        }
        int n = snprintf(reply, cap, "200 ");
        const uint8_t* p = (const uint8_t*)(uintptr_t)address;
        for (unsigned long i = 0; i < length && (unsigned)n + 3 < cap; ++i) n += snprintf(reply + n, cap - (unsigned)n, "%02x", p[i]);
        return true;
    }
    if (!strcmp(verb, "POKE")) {
        unsigned long address = 0;
        char hex[2 * LINK_PEEK_MAX + 1];
        int length;
        if (sscanf(args, "%lx %512s", &address, hex) != 2 || (length = ParseHex(hex, sPokeBytes, LINK_PEEK_MAX)) <= 0) {
            snprintf(reply, cap, "501 POKE <hex address> <hex bytes>");
            return true;
        }
        if (!MemoryOk((uintptr_t)address, (unsigned)length, true)) {
            snprintf(reply, cap, "550 Not writable.");
            return true;
        }
        sPokeAddress = (uintptr_t)address;
        sPokeLength = (unsigned)length;
        RunOnLogicThread(LINK_OP_POKE, reply, cap);
        return true;
    }
    return false;
}

/* ---- the logic thread's side ---------------------------------------------------------------- */

/* The state the request is about: number n (1 = newest) of the list. False and the reply when
 * there is no such entry. */
static bool PickState(int number, bool newIsFine) {
    if (!Port_SaveState_Scanned()) {
        Port_SaveState_RequestScan();
        snprintf(sResult, sizeof(sResult), "451 The state list was not scanned yet; try again.");
        return false;
    }
    if (number <= 0 && newIsFine) return true;
    if (number <= 0) number = 1;
    PortSaveStateInfo info;
    if (!Port_SaveState_GetInfoAt(number - 1, &info)) {
        snprintf(sResult, sizeof(sResult), "550 No state number %d (%d saved).", number, Port_SaveState_Count());
        return false;
    }
    sStateId = info.id;
    return true;
}

/* Starts a request. True when it is complete (sResult set); false when it needs more frames
 * (Continue finishes it). */
static bool Start(LinkOp op) {
    switch (op) {
        case LINK_OP_TOUCH:
            Port_BottomUI_HandleTouchDrag(sArgA, sArgB, true);
            return false; /* released next frame */
        case LINK_OP_SHOT: {
            mkdir(LINK_DEBUG_DIR, 0777);
            const bool ok = PlatformGpu3DS_SaveShots(LINK_DEBUG_DIR "/shot-top.bmp", LINK_DEBUG_DIR "/shot-bottom.bmp");
            snprintf(sResult, sizeof(sResult), ok ? "200 debug/shot-top.bmp debug/shot-bottom.bmp"
                                                  : "550 Could not take the screenshots (no GPU frame yet?).");
            return true;
        }
        case LINK_OP_DUMP:
            PlatformGpu3DS_DumpScreens();
            snprintf(sResult, sizeof(sResult), "200 Screen dump written to debug/.");
            return true;
        case LINK_OP_STATES: {
            if (!Port_SaveState_Scanned()) {
                Port_SaveState_RequestScan();
                snprintf(sResult, sizeof(sResult), "451 The state list was not scanned yet; try again.");
                return true;
            }
            const int count = Port_SaveState_Count();
            int len = snprintf(sResult, sizeof(sResult), "200 %d:", count);
            for (int i = 0; i < count && len + 24 < (int)sizeof(sResult); ++i) {
                PortSaveStateInfo info;
                if (!Port_SaveState_GetInfoAt(i, &info)) break;
                len += snprintf(sResult + len, sizeof(sResult) - (unsigned)len, " %d=%u/%02u%s", i + 1, (unsigned)info.area,
                                (unsigned)info.room, info.compatible ? "" : "(other build)");
            }
            return true;
        }
        case LINK_OP_STATE_SAVE:
            if (!PickState(sArgA, true)) return true;
            if (!Port_SaveState_Available()) {
                snprintf(sResult, sizeof(sResult), "451 A state can only be saved in gameplay.");
                return true;
            }
            if (sArgA > 0) Port_SaveState_RequestOverwrite(sStateId);
            else Port_SaveState_RequestSaveNew();
            return false;
        case LINK_OP_STATE_LOAD:
            if (!PickState(sArgA, false)) return true;
            Port_SaveState_RequestLoad(sStateId);
            return false;
        case LINK_OP_STATE_DELETE:
            if (!PickState(sArgA, false)) return true;
            Port_SaveState_RequestDelete(sStateId);
            return false;
        case LINK_OP_SWITCH: FlipSwitch(sArgA); return true;
        case LINK_OP_STATE_LINE: {
            char line[LINK_RESULT_MAX - 40];
            Port_DebugLink_StateLine(line, sizeof(line));
            snprintf(sResult, sizeof(sResult), "200 frame=%llu %s", (unsigned long long)sFrame, line);
            return true;
        }
        case LINK_OP_POKE:
            memcpy((void*)sPokeAddress, sPokeBytes, sPokeLength);
            snprintf(sResult, sizeof(sResult), "200 Wrote %u bytes.", sPokeLength);
            return true;
        default: return true;
    }
}

/* Finishes a request that waited for the save-state job to run (the job starts a few frames
 * after it was queued, at the top of the game's loop). */
static bool Continue(LinkOp op) {
    switch (op) {
        case LINK_OP_TOUCH:
            Port_BottomUI_TouchReleased();
            snprintf(sResult, sizeof(sResult), "200 Tapped %d,%d.", sArgA, sArgB);
            return true;
        case LINK_OP_STATE_SAVE:
        case LINK_OP_STATE_LOAD:
        case LINK_OP_STATE_DELETE: {
            if (Port_SaveState_IsBusy() && ++sActiveWaited < 600) return false;
            const char* message = Port_SaveState_LastMessage();
            const bool failed = strstr(message, "FALLIDO") || strstr(message, "OTRA") || strstr(message, "INCOMPAT") ||
                                strstr(message, "CORRUPTO") || strstr(message, "NO ENCONTRADO") || strstr(message, "SIN MEM") ||
                                strstr(message, "SOLO DURANTE") || sActiveWaited >= 600;
            snprintf(sStateMessage, sizeof(sStateMessage), "%s", sActiveWaited >= 600 ? "the job did not finish" : message);
            snprintf(sResult, sizeof(sResult), "%s %s", failed ? "550" : "200", sStateMessage);
            return true;
        }
        default: return true;
    }
}

void DebugLink_Poll(uint32_t* held, uint32_t* down) {
    ++sFrame;
    /* Buttons. A new KEYS first releases what is still held (one frame), so that the same
     * button pressed again is a new press for the game. */
    if (sKeysNew && sKeysNow) {
        sKeysNow = 0;
    } else {
        if (__atomic_exchange_n(&sKeysNew, false, __ATOMIC_ACQ_REL)) sKeysUntil = sFrame + sKeysFrames;
        const uint32_t want = sFrame < sKeysUntil ? sKeysMask : 0;
        *down |= want & ~sKeysNow;
        sKeysNow = want;
    }
    *held |= sKeysNow;

    if (sActive != LINK_OP_NONE) {
        if (Continue(sActive)) {
            sActive = LINK_OP_NONE;
            LightEvent_Signal(&sDone);
        }
        return;
    }
    const LinkOp op = __atomic_exchange_n(&sOp, LINK_OP_NONE, __ATOMIC_ACQ_REL);
    if (op == LINK_OP_NONE) return;
    sActiveWaited = 0;
    if (Start(op)) LightEvent_Signal(&sDone);
    else sActive = op;
}
#endif /* PORT_DEBUG_TOOLS */
