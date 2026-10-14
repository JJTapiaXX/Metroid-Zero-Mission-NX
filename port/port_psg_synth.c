#include "port_psg_synth.h"

#include "port_gba_mem.h"

/*
 * See port_psg_synth.h for why this exists. Register layout follows GBATek's
 * description of the GBA sound registers; nothing here is derived from the
 * game's data, only from the documented hardware behaviour.
 *
 * Register offsets are relative to the I/O base, matching gIoMem's indexing
 * (gIoMem[x] == GBA address 0x04000000 + x, see port_gba_mem.h).
 */
#define REG_NR10   0x60 /* ch1 sweep                       (u8)  */
#define REG_NR11   0x62 /* ch1 duty/length + envelope      (u16) */
#define REG_NR13   0x64 /* ch1 frequency + control         (u16) */
#define REG_NR21   0x68 /* ch2 duty/length + envelope      (u16) */
#define REG_NR23   0x6C /* ch2 frequency + control         (u16) */
#define REG_NR30   0x70 /* ch3 enable                      (u8)  */
#define REG_NR31   0x72 /* ch3 length + volume             (u16) */
#define REG_NR33   0x74 /* ch3 frequency + control         (u16) */
#define REG_NR41   0x78 /* ch4 length + envelope           (u16) */
#define REG_NR43   0x7C /* ch4 noise params + control      (u16) */
#define REG_SOUNDCNT_L_ 0x80 /* master volume + per-channel panning */
#define REG_SOUNDCNT_H_ 0x82 /* PSG/DMA volume ratios */
#define REG_SOUNDCNT_X_ 0x84 /* master enable */
#define REG_WAVE_RAM    0x90 /* 16 bytes = 32 nibbles */

/*
 * Amplitude of one unit of channel volume, in 16-bit output units. Each voice
 * emits +/-volume with volume in 0..15, so four voices at full tilt sum to
 * +/-60 units. At this scale that is +/-18000, comfortably inside the s16
 * range once added to the Direct Sound PCM (which reaches +/-32768) with
 * saturation. Tunable: raise if the PSG half of the music sits too low in the
 * mix against the sampled instruments, lower if the sum clips audibly.
 */
#define PSG_UNIT_SCALE 300

/* Fixed-point phase: a full period is 2^32, so the increment for a frequency
 * f at sample rate r is f/r * 2^32, computed in 64-bit to keep precision at
 * the low frequencies the wave channel reaches. */
typedef unsigned long long u64_t;

struct SquareState {
    unsigned int phase;
    unsigned int inc;
    unsigned int duty;      /* 0..3 */
    int volume;             /* current envelope volume 0..15 */
    unsigned int envStep;   /* 0 = envelope off */
    int envDir;             /* +1 / -1 */
    unsigned int envCount;  /* samples until next envelope step */
    unsigned int envPeriod; /* samples per envelope step */
    unsigned int lastCtrl;  /* last seen frequency/control register value */
    unsigned int lastEnv;   /* last seen envelope register byte */
    int enabledL, enabledR;
};

struct WaveState {
    unsigned int phase;
    unsigned int inc;
    int volumeShift;        /* 4 = mute, 0 = 100%, 1 = 50%, 2 = 25% */
    int enabled;
    unsigned int lastCtrl;
    int enabledL, enabledR;
};

struct NoiseState {
    unsigned int phase;
    unsigned int inc;
    unsigned int lfsr;
    int width7;
    int volume;
    unsigned int envStep;
    int envDir;
    unsigned int envCount;
    unsigned int envPeriod;
    unsigned int lastCtrl;
    unsigned int lastEnv;
    int enabledL, enabledR;
};

static struct SquareState sSq[2];
static struct WaveState sWave;
static struct NoiseState sNoise;
static int sMasterOn;
static int sMasterVolL, sMasterVolR; /* 0..7 */
static int sPsgRatioNum;             /* 1, 2 or 4 -> 25%, 50%, 100% */

static unsigned int RegU8(unsigned int off) {
    return gIoMem[off];
}

static unsigned int RegU16(unsigned int off) {
    return (unsigned int)gIoMem[off] | ((unsigned int)gIoMem[off + 1] << 8);
}

void PortPsg_Reset(void) {
    for (int i = 0; i < 2; ++i) {
        struct SquareState* s = &sSq[i];
        s->phase = s->inc = s->duty = 0;
        s->volume = 0;
        s->envStep = s->envCount = s->envPeriod = 0;
        s->envDir = -1;
        s->lastCtrl = 0xFFFFFFFFu;
        s->lastEnv = 0xFFFFFFFFu;
        s->enabledL = s->enabledR = 0;
    }
    sWave.phase = sWave.inc = 0;
    sWave.volumeShift = 4;
    sWave.enabled = 0;
    sWave.lastCtrl = 0xFFFFFFFFu;
    sWave.enabledL = sWave.enabledR = 0;

    sNoise.phase = sNoise.inc = 0;
    sNoise.lfsr = 0x7FFF;
    sNoise.width7 = 0;
    sNoise.volume = 0;
    sNoise.envStep = sNoise.envCount = sNoise.envPeriod = 0;
    sNoise.envDir = -1;
    sNoise.lastCtrl = 0xFFFFFFFFu;
    sNoise.lastEnv = 0xFFFFFFFFu;
    sNoise.enabledL = sNoise.enabledR = 0;

    sMasterOn = 0;
    sMasterVolL = sMasterVolR = 0;
    sPsgRatioNum = 4;
}

/*
 * Envelope register byte (NRx2): bits 4-7 initial volume, bit 3 direction,
 * bits 0-2 step time in 1/64s units.
 *
 * This engine runs its own envelope in software and rewrites this byte
 * whenever the level changes, with step time left at 0. Reloading whenever the
 * byte CHANGES (not only on a retrigger) is therefore what tracks it, and it
 * is what the port must do: reloading only on the retrigger bit misses the
 * level entirely, because unk_5104 clears that bit from its stored copy right
 * after writing the registers, so by the time a block is rendered the register
 * usually reads back with the bit already clear. That mistake kept every voice
 * at volume 0 on the first cut of this synthesiser -- the registers showed
 * real notes (e.g. noise at volume 7) while the output stayed silent.
 */
static void LoadEnvelope(unsigned int envByte, unsigned int rate, int* volume,
                         unsigned int* envStep, int* envDir,
                         unsigned int* envPeriod, unsigned int* envCount) {
    *volume = (int)((envByte >> 4) & 0xF);
    *envDir = (envByte & 0x8) ? +1 : -1;
    *envStep = envByte & 0x7;
    *envPeriod = (*envStep != 0) ? (rate * *envStep) / 64u : 0u;
    *envCount = *envPeriod;
}

static void StepEnvelope(int* volume, unsigned int envPeriod, int envDir,
                         unsigned int* envCount) {
    if (envPeriod == 0) return;
    if (--(*envCount) != 0) return;
    *envCount = envPeriod;
    const int v = *volume + envDir;
    if (v >= 0 && v <= 15) *volume = v;
}

static int sScaleL, sScaleR;
static bool sAnyVoiceActive;

void PortPsg_BeginBlock(unsigned int rate) {
    if (rate == 0) return;

#ifdef PORT_DEBUG_TOOLS
    /* Which voices does the engine ACTUALLY drive? This distinguishes "the
     * engine never starts a note on this channel" from "the synthesis is
     * wrong" -- both sound identical (silence), which is what made the first
     * cut of this file hard to judge. Logs only when the set of
     * ever-non-silent channels grows, so it costs a handful of SD writes for
     * a whole session rather than one per block. Off unless LOG mode is ALL
     * or AUDIO. */
    {
        extern void Port_DebugLog_Audio(const char* msg);
        static unsigned int sSeenMask;
        unsigned int mask = sSeenMask;
        if (((RegU16(REG_NR11) >> 12) & 0xF) != 0) mask |= 1u;
        if (((RegU16(REG_NR21) >> 12) & 0xF) != 0) mask |= 2u;
        if ((RegU8(REG_NR30) & 0x80) && (((RegU16(REG_NR31) >> 13) & 0x3) != 0)) mask |= 4u;
        if (((RegU16(REG_NR41) >> 12) & 0xF) != 0) mask |= 8u;
        if (mask != sSeenMask) {
            sSeenMask = mask;
            char msg[120];
            __builtin_snprintf(msg, sizeof(msg),
                "psg: voices seen so far: sq1=%d sq2=%d wave=%d noise=%d (pan=%02X)",
                (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0, (mask & 8) != 0,
                gIoMem[REG_SOUNDCNT_L_ + 1]);
            Port_DebugLog_Audio(msg);
        }
    }
#endif

    const unsigned int cntX = RegU8(REG_SOUNDCNT_X_);
    sMasterOn = (cntX & 0x80) != 0;

    const unsigned int cntL = RegU16(REG_SOUNDCNT_L_);
    sMasterVolR = (int)(cntL & 0x7);
    sMasterVolL = (int)((cntL >> 4) & 0x7);

    /* Per-channel panning: bits 8-11 enable right, bits 12-15 enable left
     * (this is the byte the engine writes through REG_SOUNDCNT_L + 1, see
     * UpdatePsgSounds in src/audio.c). */
    const unsigned int panR = (cntL >> 8) & 0xF;
    const unsigned int panL = (cntL >> 12) & 0xF;

    /* PSG:DMA volume ratio, SOUNDCNT_H bits 0-1: 0 = 25%, 1 = 50%, 2 = 100%. */
    switch (RegU16(REG_SOUNDCNT_H_) & 0x3) {
        case 0:  sPsgRatioNum = 1; break;
        case 1:  sPsgRatioNum = 2; break;
        default: sPsgRatioNum = 4; break;
    }

    /* --- Square channels 1 and 2 --- */
    static const unsigned int kDutyReg[2] = { REG_NR11, REG_NR21 };
    static const unsigned int kCtrlReg[2] = { REG_NR13, REG_NR23 };
    for (int i = 0; i < 2; ++i) {
        struct SquareState* s = &sSq[i];
        const unsigned int duty = RegU16(kDutyReg[i]);
        const unsigned int ctrl = RegU16(kCtrlReg[i]);

        s->duty = (duty >> 6) & 0x3;
        s->enabledR = (panR >> i) & 1;
        s->enabledL = (panL >> i) & 1;

        const unsigned int freq = ctrl & 0x7FF;
        if (freq < 2048u) {
            /* f = 131072 / (2048 - freq) Hz */
            s->inc = (unsigned int)((131072ull << 32) / ((u64_t)(2048u - freq) * rate));
        } else {
            s->inc = 0;
        }

        const unsigned int envByte = (duty >> 8) & 0xFF;
        if (envByte != s->lastEnv) {
            s->lastEnv = envByte;
            LoadEnvelope(envByte, rate, &s->volume, &s->envStep,
                         &s->envDir, &s->envPeriod, &s->envCount);
        }
        if (ctrl != s->lastCtrl) {
            s->lastCtrl = ctrl;
            if (ctrl & 0x8000) s->phase = 0;
        }
    }

    /* --- Wave channel 3 --- */
    {
        const unsigned int nr30 = RegU8(REG_NR30);
        const unsigned int nr31 = RegU16(REG_NR31);
        const unsigned int ctrl = RegU16(REG_NR33);

        sWave.enabled = (nr30 & 0x80) != 0;
        sWave.enabledR = (panR >> 2) & 1;
        sWave.enabledL = (panL >> 2) & 1;

        /* NR32 bits 5-6 (u16 bits 13-14): 0 mute, 1 100%, 2 50%, 3 25%.
         * GBA adds a "force 75%" bit (u16 bit 15) which overrides those. */
        if (nr31 & 0x8000) {
            sWave.volumeShift = -1; /* 75%, handled specially below */
        } else {
            switch ((nr31 >> 13) & 0x3) {
                case 0:  sWave.volumeShift = 4; break; /* mute */
                case 1:  sWave.volumeShift = 0; break; /* 100% */
                case 2:  sWave.volumeShift = 1; break; /* 50%  */
                default: sWave.volumeShift = 2; break; /* 25%  */
            }
        }

        const unsigned int freq = ctrl & 0x7FF;
        if (freq < 2048u) {
            /* One full 32-step cycle at 65536/(2048-freq) Hz. */
            sWave.inc = (unsigned int)((65536ull << 32) / ((u64_t)(2048u - freq) * rate));
        } else {
            sWave.inc = 0;
        }

        if (ctrl != sWave.lastCtrl) {
            sWave.lastCtrl = ctrl;
            if (ctrl & 0x8000) sWave.phase = 0;
        }
    }

    /* --- Noise channel 4 --- */
    {
        const unsigned int nr41 = RegU16(REG_NR41);
        const unsigned int ctrl = RegU16(REG_NR43);

        sNoise.enabledR = (panR >> 3) & 1;
        sNoise.enabledL = (panL >> 3) & 1;
        sNoise.width7 = (ctrl & 0x8) != 0;

        /* f = 524288 / r / 2^(s+1), with r = 0 meaning 0.5 (GBATek). */
        const unsigned int r = ctrl & 0x7;
        const unsigned int s = (ctrl >> 4) & 0xF;
        unsigned int freqHz;
        if (r == 0)
            freqHz = 1048576u >> (s + 1);
        else
            freqHz = (524288u / r) >> (s + 1);
        if (freqHz == 0) freqHz = 1;
        sNoise.inc = (unsigned int)(((u64_t)freqHz << 32) / rate);

        const unsigned int envByte = (nr41 >> 8) & 0xFF;
        if (envByte != sNoise.lastEnv) {
            sNoise.lastEnv = envByte;
            LoadEnvelope(envByte, rate, &sNoise.volume, &sNoise.envStep,
                         &sNoise.envDir, &sNoise.envPeriod, &sNoise.envCount);
        }
        if (ctrl != sNoise.lastCtrl) {
            sNoise.lastCtrl = ctrl;
            if (ctrl & 0x8000) sNoise.lfsr = 0x7FFF;
        }
    }

    /* Compute master output scale for left and right (replaces software division in NextSample) */
    sScaleL = (PSG_UNIT_SCALE * sPsgRatioNum * (sMasterVolL + 1));
    sScaleR = (PSG_UNIT_SCALE * sPsgRatioNum * (sMasterVolR + 1));

    /* Check if any voice is actually producing sound */
    sAnyVoiceActive = sMasterOn && (
        ((sSq[0].enabledL || sSq[0].enabledR) && sSq[0].inc != 0 && sSq[0].volume != 0) ||
        ((sSq[1].enabledL || sSq[1].enabledR) && sSq[1].inc != 0 && sSq[1].volume != 0) ||
        (sWave.enabled && (sWave.enabledL || sWave.enabledR) && sWave.inc != 0 && sWave.volumeShift != 4) ||
        ((sNoise.enabledL || sNoise.enabledR) && sNoise.inc != 0 && sNoise.volume != 0)
    );
}

bool PortPsg_IsAnyVoiceActive(void) {
    return sAnyVoiceActive;
}

/* Duty thresholds as a fraction of the 32-bit phase: 12.5%, 25%, 50%, 75%. */
static const unsigned int kDutyThreshold[4] = {
    0x20000000u, 0x40000000u, 0x80000000u, 0xC0000000u
};

void PortPsg_NextSampleGain(int* outLeft,int* outRight,const unsigned gains[4]) {
    int left = 0, right = 0;

    if (!sAnyVoiceActive) {
        *outLeft = 0;
        *outRight = 0;
        return;
    }

    for (int i = 0; i < 2; ++i) {
        struct SquareState* s = &sSq[i];
        s->phase += s->inc;
        StepEnvelope(&s->volume, s->envPeriod, s->envDir, &s->envCount);
        if (s->inc == 0 || s->volume == 0) continue;
        const int amp = ((s->phase < kDutyThreshold[s->duty]) ? s->volume : -s->volume)*(int)(gains[i]/25);
        if (s->enabledL) left += amp;
        if (s->enabledR) right += amp;
    }

    if (sWave.enabled && sWave.inc != 0 && sWave.volumeShift != 4) {
        sWave.phase += sWave.inc;
        /* Top 5 bits of the phase select one of the 32 nibbles. */
        const unsigned int idx = sWave.phase >> 27;
        const unsigned int byte = gIoMem[REG_WAVE_RAM + (idx >> 1)];
        const int nibble = (idx & 1) ? (int)(byte & 0xF) : (int)(byte >> 4);
        /* Nibbles are unsigned 0..15; centre them so silence is 0. */
        int amp = nibble - 8;
        if (sWave.volumeShift < 0)
            amp = (amp * 3) >> 2; /* GBA 75% mode */
        else
            amp >>= sWave.volumeShift;
        amp=amp*(int)(gains[2]/25);
        if (sWave.enabledL) left += amp;
        if (sWave.enabledR) right += amp;
    } else {
        sWave.phase += sWave.inc;
    }

    {
        StepEnvelope(&sNoise.volume, sNoise.envPeriod, sNoise.envDir, &sNoise.envCount);
        const unsigned int before = sNoise.phase;
        sNoise.phase += sNoise.inc;
        if (sNoise.phase < before) {
            const unsigned int bit = (sNoise.lfsr ^ (sNoise.lfsr >> 1)) & 1u;
            sNoise.lfsr >>= 1;
            sNoise.lfsr |= bit << 14;
            if (sNoise.width7) {
                sNoise.lfsr &= ~(1u << 6);
                sNoise.lfsr |= bit << 6;
            }
        }
        if (sNoise.volume != 0) {
            const int amp = ((sNoise.lfsr & 1u) ? -sNoise.volume : sNoise.volume)*(int)(gains[3]/25);
            if (sNoise.enabledL) left += amp;
            if (sNoise.enabledR) right += amp;
        }
    }

    /* Scale into 16-bit output units without software divide */
    *outLeft = (left * sScaleL) >> 7;
    *outRight = (right * sScaleR) >> 7;
}

void PortPsg_NextSample(int* outLeft,int* outRight) {
    static const unsigned gains[4]={100,100,100,100};
    PortPsg_NextSampleGain(outLeft,outRight,gains);
}
