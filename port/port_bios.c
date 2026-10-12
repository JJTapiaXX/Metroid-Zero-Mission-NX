/*
 * Clean-room C reimplementation of the GBA BIOS routines mzm calls as plain
 * functions (CpuSet, Div, LZ77 decompression, Sqrt, ...) via the thin
 * asm/syscalls.s trampolines (`swi SYSCALL_X; bx lr`). Real hardware `swi`
 * traps into the GBA BIOS; on 3DS the same instruction traps into Horizon
 * OS's own syscall table instead, which does something unrelated (and,
 * depending on the immediate, may be flatly illegal from user mode) --
 * silently "compiles fine, corrupts memory or crashes on real hardware"
 * territory, not a build error. asm/syscalls.s is excluded from the 3DS
 * build; this file provides the same symbols.
 *
 * Algorithms here follow the public GBA BIOS behavior as documented by
 * GBATek (the community hardware reference) -- register-level semantics,
 * not decompiled/copied code.
 */
#include "types.h"

#include <string.h>
#include <stdlib.h>

s32 DivarmDiv(s32 num, s32 denom) {
    return denom != 0 ? num / denom : 0;
}

s32 DivarmMod(s32 num, s32 denom) {
    return denom != 0 ? num % denom : 0;
}

/* Signatures below match include/syscalls.h exactly (reproduced verbatim
 * in the 3DS shadow copy, port/generated/shadow/syscalls.h) -- every
 * gameplay .c still includes that header for these prototypes, so a
 * mismatch here would be a hard compile error, not just a style nit. */

u16 Sqrt(u32 value) {
    u32 result = 0;
    u32 bit = 1u << 30;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        u32 candidate = result + bit;
        if (value >= candidate) {
            value -= candidate;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

/* ctrl bit layout (GBATek "3.4 CpuSet/CpuFastSet"):
 *   0-20  word count
 *   24    datasize: 0 = 16-bit, 1 = 32-bit (ignored by CpuFastSet, always 32)
 *   25    fixed source: 0 = copy, 1 = fill (repeat src[0]) */
#define CPUSET_COUNT_MASK 0x001FFFFFu
#define CPUSET_32BIT (1u << 24)
#define CPUSET_FIXED_SRC (1u << 25)

#include "port_gba_mem.h"

void CpuSet(void* src, void* dst, u32 ctrl) {
    u32 count = ctrl & CPUSET_COUNT_MASK;
    int fill = (ctrl & (CPUSET_FIXED_SRC | (1u << 24))) != 0;
    u32 i;

    void* d = port_resolve_write_addr((uintptr_t)dst);
    const void* s = port_resolve_copy_src(src, count * 4);
    if (!d || !s) return;

    if (ctrl & CPUSET_32BIT) {
        const u32* src32 = (const u32*)s;
        u32* dst32 = (u32*)d;
        for (i = 0; i < count; i++) {
            dst32[i] = fill ? src32[0] : src32[i];
        }
    } else {
        const u16* src16 = (const u16*)s;
        u16* dst16 = (u16*)d;
        for (i = 0; i < count; i++) {
            dst16[i] = fill ? src16[0] : src16[i];
        }
    }
}

void CpuFastSet(void* src, void* dst, u32 ctrl) {
    u32 count = (((u32)ctrl & CPUSET_COUNT_MASK) + 7u) & ~7u;
    int fill = (ctrl & (CPUSET_FIXED_SRC | (1u << 24))) != 0;
    u32 i;

    void* d = port_resolve_write_addr((uintptr_t)dst);
    const void* s = port_resolve_copy_src(src, count * 4);
    if (!d || !s) return;

    const u32* src32 = (const u32*)s;
    u32* dst32 = (u32*)d;
    for (i = 0; i < count; i++) {
        dst32[i] = fill ? src32[0] : src32[i];
    }
}

/* Standard GBA LZ77 stream: 4-byte header (0x10, then 24-bit LE
 * decompressed size), followed by blocks of one flag byte (MSB-first, 1 bit
 * per token) and tokens:
 *   flag bit 0 -> literal byte, copied as-is
 *   flag bit 1 -> back-reference: 2 bytes, big-endian-ish packed as
 *                 (length-3):4 bits high nibble, (distance-1):12 bits
 * Real hardware processes VRAM in 16-bit units because the GBA's VRAM bus
 * doesn't support 8-bit writes; our "VRAM" is a plain RAM buffer (see
 * port_gba_mem.c), so both variants share one byte-addressable
 * implementation -- there's no bus-width hazard to work around here. */
static void Lz77Uncomp(const u8* src, u8* dst) {
    const u8* s = (const u8*)port_resolve_copy_src(src, 4);
    u8* d = (u8*)port_resolve_write_addr((uintptr_t)dst);
    int bit;
    u32 i;
    if (!s || !d) return;

    u32 header = (u32)s[0] | ((u32)s[1] << 8) | ((u32)s[2] << 16) | ((u32)s[3] << 24);
    u32 decompressedSize = header >> 8;
    s += 4;

    u8* out = d;
    u8* outEnd = d + decompressedSize;

    while (out < outEnd) {
        u8 flags = *s++;
        for (bit = 7; bit >= 0 && out < outEnd; bit--) {
            if ((flags & (1 << bit)) == 0) {
                *out++ = *s++;
            } else {
                u8 b0 = *s++;
                u8 b1 = *s++;
                u32 length = (u32)(b0 >> 4) + 3;
                u32 distance = (((u32)(b0 & 0xF) << 8) | b1) + 1;
                const u8* copySrc = out - distance;
                for (i = 0; i < length && out < outEnd; i++) {
                    *out++ = *copySrc++;
                }
            }
        }
    }
}

void LZ77UncompVram(const void* src, void* dst) {
    Lz77Uncomp((const u8*)src, (u8*)dst);
}

void LZ77UncompWram(const void* src, void* dst) {
    Lz77Uncomp((const u8*)src, (u8*)dst);
}

/* Multiplayer boot: no multiplayer on this port, always report failure. */
s32 Multiboot(void* mbp) {
    (void)mbp;
    return 1;
}

/* GBA analog sound DAC bias ramp -- no equivalent on 3DS's own audio
 * hardware, safe no-ops. */
void SoundBias0(void) {}
void SoundBias200(void) {}

/* Real MidiKey2Freq semantics: calculates sample playback frequency in Hz
 * given a ToneData/WaveData pointer, MIDI key (0-127, 60=middle C), and
 * fine tune adjust (0-255). waveData[1] is the base frequency stored in
 * the sample header in 1024ths of a Hz (i.e. Hz * 1024). */
u32 MidiKey2Freq(u32* waveData, u8 midiKey, u8 fineAdjust) {
    if (!waveData)
        return 0;

    const u32* wd = (const u32*)GBA_RESOLVE((const void*)waveData);
    u32 baseFreq = wd[1]; // base frequency stored as (Hz * 1024)
    if (baseFreq == 0)
        return 0;

    double semitones = (double)(midiKey - 60) + (double)fineAdjust / 256.0;
    double freq = ((double)baseFreq * __builtin_exp2(semitones / 12.0)) / 1024.0;
    return (u32)(freq + 0.5);
}

extern void CallbackCallVblank(void);

/* Deliberately not `#include <3ds.h>`: 3ds/types.h typedefs u32/s32/vu32/...
 * as (distinct, same-size) aliases of the stdint types, which conflicts
 * with mzm's own types.h (typedef'd from the plain C integer types) the
 * moment both are visible in one translation unit. Forward-declare just the
 * one libctru function actually needed instead of pulling in the whole
 * header -- real signature per libctru's gspgpu.h. */
/* gspWaitForEvent forward declaration (GSPGPU_EVENT_VBlank0 = 0). Currently
 * unused below -- see the "temporarily" comment at the Port_Bios_Halt call
 * site -- kept forward-declared for when real GPU presentation work makes
 * genuine vblank sync necessary again. */
extern void gspWaitForEvent(int event, bool nextEvent);
extern void svcSleepThread(long long ns);

/* aptMainLoop forward declaration. Returns false once the user requests the
 * app close (HOME menu "Close", sleep-then-close, power button, ...). Never
 * having called this meant the app couldn't respond to HOME at all -- the
 * only way out was powering off the console. Also pumps APT's internal
 * event queue, which real hardware needs serviced regularly for good
 * cooperative behavior with the system (untested whether this is also
 * needed for gspWaitForEvent to ever unblock -- see the call site). */
extern bool aptMainLoop(void);
extern void gfxExit(void);

/* Synchronizes the GBA-side cooperative loop to real 60Hz vblank timing.
 * agbmain()'s frame loop is:
 *     do { SYSCALL(2); } while (!(gVBlankRequestFlag & 1));
 * On real hardware, SYSCALL(2) (Halt) suspends the CPU until any interrupt
 * fires; the vblank ISR (CallbackCallVblank, src/callbacks.c) then sets
 * gVBlankRequestFlag, and the loop re-checks and exits. There's no
 * interrupt controller here to fire that ISR automatically, so this
 * function does both halves itself: wait for the real vblank, then invoke
 * the same ISR body directly. include/syscalls.h is shadowed (see
 * port/generated/shadow/syscalls.h) to route SYSCALL(num) here for every
 * num -- the two calls to SYSCALL(2)/(3) that exist across the whole
 * codebase both use it as a wait-for-next-vblank point, not to
 * differentiate GBA's Halt from Stop. */
#if defined(PLATFORM_LINUX)
extern void Platform_Linux_VBlank(void);
#endif

#ifdef MZM_3DS
extern void Port_DebugLog(const char* msg);
extern void Port_PPU_RenderFrame(void);
extern void Platform3DS_PollKeysIntoGba(void);
extern void PlatformGpu3DS_RecordTick(void);
extern void PlatformGpu3DS_PerfRecordTick(void);
extern u64 Platform3DS_SystemTick(void);
extern u64 Platform3DS_TicksPerSecond(void);
extern void Platform3DS_WaitForVBlank(void);

/* Guards gMusicInfo/TrackData against the audio thread's own production
 * ticks (see platform/3ds/source/port_mzm_audio_3ds.c's doc comment on
 * sAudioStateLock). Released for the render+pace work below, which never
 * touches audio state, so the audio thread gets a real window to run in
 * every VBlankIntrWait() call regardless of how deep in game logic it was
 * called from (src/transfer.c calls it directly mid-frame, not just
 * agbmain's own loop -- see Port_PPU_RenderFrame's reentrancy comment). */
extern void Port_AudioStateLock_Acquire(void);
extern void Port_AudioStateLock_Release(void);

/* Real GBA frame period: 228 scanlines * 73350ns/scanline (see
 * port/port_gba_timing.c for where that constant comes from) ~= 59.79Hz.
 * Port_PPU_RenderFrame() no longer blocks on the GPU (see
 * platform/3ds/source/port_ppu_mzm.c's Port_PPU_RenderFrame/
 * Port_PPU_GpuPresentPump split), so nothing paces agbmain()'s loop -- and
 * with it game logic and audio production -- to real time anymore unless
 * this does it explicitly. Deadline-accumulates instead of sleeping a fixed
 * amount every call, so small per-call jitter doesn't drift the average
 * rate; resyncs instead of burning through a backlog if a single iteration
 * falls more than one frame behind (a debugger breakpoint, a slow SD
 * write, ...), rather than free-running to catch up. */
#define PORT_BIOS_FRAME_NS 16724400ull
static u64 sNextFrameDeadlineTicks;

/* ---- Render throttle for the GPU path --------------------------------
 * The GPU renderer's C3D_FrameSync() is the frame pacer: one game-logic
 * tick (CallbackCallVblank) per presented frame. When a frame overruns the
 * 16.7ms vblank budget C3D_FrameSync blocks to the NEXT vblank, so the
 * whole loop -- logic included -- drops to 30Hz and the game runs in slow
 * motion (audio stays real-time on its own thread, so it desyncs).
 *
 * A "sim clock" tracks where game time has got to; every logic tick (never
 * skipped) advances it one frame. The render is what gets skipped:
 *
 *   ADAPTIVE  -- render whenever the sim is not behind real time; while it
 *                is behind, skip the render and RETURN FAST so agbmain runs
 *                the next logic tick and the sim catches up. Fluid, and its
 *                rate settles at whatever the scene sustains.
 *   LOCKED 30 -- render on every other logic tick and, on the skipped tick,
 *                sleep just until real time reaches the sim clock. A fast
 *                scene pair is render(~16.7) + sleep(~16.7) = 33.3ms; a
 *                heavy pair is render(~33, two vblanks) + sleep(~0) = 33.3.
 *                Steady 30 Hz picture, 60 Hz logic, either way.
 *
 * Both resync (rather than chase a huge backlog) after a stall -- debugger,
 * slow SD write. */
static u64 sSimClockTicks;      /* where game time has reached */
static int sLocked30Parity;
static bool sGpuFrameSkipEnabled = true;
extern int Port_Config_GetFramePacing(void); /* 0 adaptive, 1 locked 30 */

void Port_Bios_SetAdaptiveFrameSkip(bool on) {
    sGpuFrameSkipEnabled = on;
    sSimClockTicks = 0;
}
bool Port_Bios_AdaptiveFrameSkipEnabled(void) { return sGpuFrameSkipEnabled; }

/* Decides skip for the logic tick that just ran, and for LOCKED 30 also
 * paces the skipped tick. Returns true to skip the render. */
static bool Port_Bios_DecideRenderSkip(bool locked30) {
    if (!sGpuFrameSkipEnabled) return false;
    const u64 tps = Platform3DS_TicksPerSecond();
    const u64 tpf = (tps * PORT_BIOS_FRAME_NS) / 1000000000ull;
    const u64 now = Platform3DS_SystemTick();
    if (sSimClockTicks == 0) sSimClockTicks = now;
    sSimClockTicks += tpf;                       /* this logic tick */
    const s64 drift = (s64)(sSimClockTicks - now); /* >0: sim ahead of real time */
    if (drift < -(s64)(4ull * tpf) || drift > (s64)(4ull * tpf)) {
        sSimClockTicks = now;                    /* stall: resync, don't chase it */
        sLocked30Parity = 0;
        return false;
    }
    if (locked30) {
        sLocked30Parity ^= 1;
        if (sLocked30Parity == 0) return false;  /* render tick */
        /* Skipped tick: sleep until real time catches up to the sim clock,
         * so the render/skip pair lands on 33.3ms regardless of how long the
         * render took. Never sleeps when the render already overran. */
        if (drift > 0) {
            const s64 ns = (s64)(((u64)drift * 1000000000ull) / tps);
            if (ns > 0) svcSleepThread(ns);
        }
        return true;
    }
    /* Adaptive: render while the sim is keeping up; skip (fast) while behind. */
    return drift < 0;
}

/* ---- Temporal OAM merge for frame-skipped rendering -----------------
 * The game logic ticks at 60 Hz; under the render throttle we only sample
 * OAM every 2nd/3rd frame. MZM blinks sprites on and off every game frame
 * -- the screw-attack glow, the i-frames after damage, the save-capsule
 * shimmer -- meant to read as ~50% on a persistent LCD. Sampled at ~30 Hz
 * with a drifting phase it strobes: fully lit one render, gone the next.
 *
 * So OR sprite visibility across every game frame skipped since the last
 * render and hand the renderer that merged OAM: a sprite drawn in ANY of
 * those frames is drawn now, which turns the strobe back into a steady
 * "mostly on". Costs one 1 KB copy per game frame and nothing at a
 * sustained 60 (every frame renders -> the merge just reseeds from live).
 * A sprite that legitimately vanishes lingers at most one render (~33 ms). */
static u16 sOamMerged[0x400 / 2];
static bool sOamMergeSeeded;

static inline bool Port_Bios_OamSlotVisible(const u16* oam, int i) {
    const u16 a0 = oam[i * 4 + 0], a1 = oam[i * 4 + 1];
    const bool affine = (a0 >> 8) & 1u;
    if (((a0 >> 9) & 1u) && !affine) return false;   /* non-affine hidden bit */
    if (((a0 >> 10) & 3u) == 2u) return false;        /* OBJ window, not drawn */
    if (a0 == 0x00FFu && a1 == 0x00FFu && oam[i * 4 + 2] == 0u) return false; /* ResetFreeOam's parked slot */
    /* The WIDE view (platform/3ds/source/port_wide_view.h) shows past the GBA
     * frame, so "on screen" reaches that far too. Otherwise a sprite out in
     * the extra area counts as blinked off, and the merge draws whatever that
     * slot held a frame earlier in its place. */
    extern int Port_WideMarginSubPixelX(void);
    extern int Port_WideMarginSubPixelY(void);
    const int mx = Port_WideMarginSubPixelX() / 4, my = Port_WideMarginSubPixelY() / 4;
    int y = a0 & 0xFF; if (y >= 160 + my) y -= 256;
    int x = (int)(a1 & 0x1FF); if (x >= 240 + mx) x -= 512;
    return y > -64 - my && y < 160 + my && x > -64 - mx && x < 240 + mx;  /* roughly on screen */
}

/* Fold the current frame's OAM into sOamMerged. Called every game frame,
 * right after the logic tick, before the skip decision. */
static void Port_Bios_OamMergeTick(void) {
    const u16* cur = gOamMem;
    /* Which slots now hold this frame's entry. The WIDE view's per-slot
     * position tags have to follow the same choice: a slot kept from an
     * earlier frame must keep that frame's tag, or its sprite is placed with
     * the position of whatever sits in the slot now. */
    extern void Port_Wide_MergeTags(const bool* tookLive);
    bool took[128];
    if (!sOamMergeSeeded) {
        memcpy(sOamMerged, cur, sizeof sOamMerged);
        sOamMergeSeeded = true;
        for (int i = 0; i < 128; ++i) took[i] = true;
        Port_Wide_MergeTags(took);
        return;
    }
    for (int i = 0; i < 128; ++i) {
        took[i] = Port_Bios_OamSlotVisible(cur, i) || !Port_Bios_OamSlotVisible(sOamMerged, i);
        if (took[i])
            memcpy(&sOamMerged[i * 4], &cur[i * 4], 8);
        /* else current is a blink-off of a slot that was on -> keep it on */
    }
    Port_Wide_MergeTags(took);
}

static void Port_Bios_PaceFrame(void) {
    const u64 ticksPerSec = Platform3DS_TicksPerSecond();
    const u64 ticksPerFrame = (ticksPerSec * PORT_BIOS_FRAME_NS) / 1000000000ull;
    const u64 now = Platform3DS_SystemTick();

    if (sNextFrameDeadlineTicks == 0 || now >= sNextFrameDeadlineTicks + ticksPerFrame) {
        sNextFrameDeadlineTicks = now;
    }
    sNextFrameDeadlineTicks += ticksPerFrame;

    if (now < sNextFrameDeadlineTicks) {
        const u64 remainingTicks = sNextFrameDeadlineTicks - now;
        const s64 remainingNs = (s64)((remainingTicks * 1000000000ull) / ticksPerSec);
        if (remainingNs > 0) svcSleepThread(remainingNs);
    }
}
#endif

#if defined(MZM_3DS) && !defined(PLATFORM_LINUX)
/* When the previous Port_Bios_Halt handed control back to the game: the time
 * from there to the next Halt is game logic (perf recorder, PERF_PHASE_LOGIC). */
static u64 sHaltExitTick;
extern void PlatformGpu3DS_PerfPhaseAdd(int phase, unsigned long long ticks);
enum { PERF_PHASE_LOGIC_ID = 0 }; /* PERF_PHASE_LOGIC, platform_gpu_3ds.h */
#endif

void Port_Bios_Halt(void) {
#ifdef PLATFORM_SWITCH
    extern void GameSwitch_VBlank(void);
    GameSwitch_VBlank();
    return;
#endif
#if defined(PLATFORM_LINUX)
    Platform_Linux_VBlank();
#elif defined(MZM_3DS)
    if (sHaltExitTick != 0)
        PlatformGpu3DS_PerfPhaseAdd(PERF_PHASE_LOGIC_ID, Platform3DS_SystemTick() - sHaltExitTick);
#ifdef PORT_VERBOSE_FRAME_LOG
    Port_DebugLog("Port_Bios_Halt: before aptMainLoop");
#endif
    extern bool Platform3DS_QuitRequested(void);
    if (!aptMainLoop() || Platform3DS_QuitRequested()) {
        Port_DebugLog("Port_Bios_Halt: aptMainLoop returned false, exiting");
        gfxExit();
        exit(0);
    }
    Platform3DS_PollKeysIntoGba();
    PlatformGpu3DS_RecordTick();
    /* PlatformGpu3DS_PerfRecordTick() is deferred to the render branch below:
     * a skipped frame must not emit a sample (it has no cost data and its
     * near-zero wall time reads as a false 800 FPS). Folding it forward keeps
     * durationUs a true frame-to-frame interval. */
    /* Temporarily sleep-paced instead of gspWaitForEvent(0, true): the
     * latter never unblocks on real hardware here (confirmed via
     * <game folder>/debug/mzm-debug.log bisection -- neither the GSP-event-thread
     * priority collision theory (fixed, no change) nor missing
     * aptMainLoop() pumping (added above, no change) explained it). Since
     * nothing is actually presented to the GPU yet (port_ppu_3ds.c isn't
     * adapted -- see docs/3ds-port-skeleton-import.md), real vblank sync
     * isn't needed yet either; a plain 60Hz sleep unblocks gameplay-logic
     * testing now and can be swapped back once gfxSwapBuffers()-driven
     * presentation exists to investigate the gspWaitForEvent hang for real. */
#endif
#if defined(MZM_3DS) && !defined(PLATFORM_LINUX)
    Port_AudioStateLock_Release();
#endif
#if defined(MZM_3DS) && !defined(PLATFORM_LINUX)
    const u64 vblankStart = Platform3DS_SystemTick();
#endif
    CallbackCallVblank();
#if defined(MZM_3DS) && !defined(PLATFORM_LINUX)
    PlatformGpu3DS_PerfPhaseAdd(PERF_PHASE_LOGIC_ID, Platform3DS_SystemTick() - vblankStart);
#endif

extern bool Port_PPU_3DS_LastFrameUsedGpu(void);

#if defined(MZM_3DS) && !defined(PLATFORM_LINUX)
#ifdef PORT_VERBOSE_FRAME_LOG
    Port_DebugLog("Port_Bios_Halt: after CallbackCallVblank");
#endif
    /* Skip the render (never the logic tick above) to keep game speed
     * correct -- see Port_Bios_DecideRenderSkip for the two modes. Only
     * meaningful once the GPU renderer is actually the pacer. */
    bool skipRender = sGpuFrameSkipEnabled && Port_PPU_3DS_LastFrameUsedGpu() &&
                      Port_Bios_DecideRenderSkip(Port_Config_GetFramePacing() == 1);
    Port_Bios_OamMergeTick();
    if (!skipRender) PlatformGpu3DS_PerfRecordTick();
    if (skipRender) {
#ifdef PORT_VERBOSE_FRAME_LOG
        Port_DebugLog("Port_Bios_Halt: render SKIPPED");
#endif
        /* No submit, no C3D_FrameSync -- Port_Bios_DecideRenderSkip already
         * paced this tick (fast return for ADAPTIVE, sleep-to-sim-clock for
         * LOCKED 30). The present thread keeps showing the last frame. */
    } else {
        /* Render from the merged OAM so a sprite blinked off this frame but
         * on in a skipped one is still drawn (see Port_Bios_OamMergeTick).
         * Restore the live OAM right after -- game logic must never see the
         * merged copy. */
        u16 liveOam[0x400 / 2];
        memcpy(liveOam, gOamMem, sizeof liveOam);
        memcpy(gOamMem, sOamMerged, sizeof sOamMerged);
        Port_PPU_RenderFrame();
        memcpy(gOamMem, liveOam, sizeof liveOam);
        sOamMergeSeeded = false; /* next frame reseeds the merge from live */
#ifdef PORT_VERBOSE_FRAME_LOG
        Port_DebugLog("Port_Bios_Halt: after Port_PPU_RenderFrame");
#endif
        /* If the frame was submitted via the synchronous GPU renderer, C3D_FrameSync()
         * in PlatformGpu3DS_EndBottom has already synchronized to hardware VBlank (60Hz).
         * Only the asynchronous CPU renderer handoff path needs software pacing. */
        if (!Port_PPU_3DS_LastFrameUsedGpu()) {
            Port_Bios_PaceFrame();
        } else {
            sNextFrameDeadlineTicks = 0;
        }
    }
    Port_AudioStateLock_Acquire();
    sHaltExitTick = Platform3DS_SystemTick();
#endif
}

void VBlankIntrWait(void) {
    Port_Bios_Halt();
}
