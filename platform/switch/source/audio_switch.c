/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <switch.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdatomic.h>
#include "audio_switch.h"
#include "diagnostics.h"
#include "audio_volume.h"
#include "audio_filters.h"
#include <stdio.h>

enum { BUFFER_COUNT=8, BUFFER_FRAMES=2048 };
static AudioOutBuffer buffers[BUFFER_COUNT];
static bool queued[BUFFER_COUNT], ready, initialized;
static unsigned remainder;
static int16_t previous[2];
static Thread worker;
static atomic_bool worker_running;
static bool worker_started;
static Mutex state_mutex;
static unsigned dropped,starved;
static unsigned applied_volume=~0u;
static bool service_gain;
static unsigned output_blocks;
static unsigned gain_numerator(unsigned percent) {
    return percent*100; /* Linear master gain: 50% = 0.5, 100% = unity. */
}
static void apply_volume(void) {
    unsigned percent=AudioVolume_Get();
    if (percent==applied_volume) return;
    float requested=(float)(percent>100 ? 100 : percent)/100.0f;
    Result rc=audoutSetAudioOutVolume(requested);
    float actual=-1.0f;
    Result read_rc=R_FAILED(rc) ? rc : audoutGetAudioOutVolume(&actual);
    /* A rejected or clamped setting falls back to deterministic software gain. */
    service_gain=R_SUCCEEDED(rc) && R_SUCCEEDED(read_rc) && actual>=requested-0.00001f && actual<=requested+0.00001f;
    if (!service_gain) audoutSetAudioOutVolume(1.0f);
    applied_volume=percent;
    Diagnostics_Output(percent,gain_numerator(percent),actual,rc,service_gain,0);
}
uint64_t AudioSwitch_NowNs(void) { return armTicksToNs(armGetSystemTick()); }
static void audio_worker(void *unused) {
    (void)unused;
    extern void GameSwitch_AudioTick(void);
    uint64_t deadline=armTicksToNs(armGetSystemTick());
    while (atomic_load(&worker_running)) {
        mutexLock(&state_mutex);
        GameSwitch_AudioTick();
        mutexUnlock(&state_mutex);
        deadline+=16742706;
        uint64_t now=armTicksToNs(armGetSystemTick());
        if (now<deadline) svcSleepThread(deadline-now);
        else if (now>deadline+33485412) deadline=now;
    }
}
void AudioSwitch_LockState(void) { mutexLock(&state_mutex); }
void AudioSwitch_UnlockState(void) { mutexUnlock(&state_mutex); }
bool AudioSwitch_StartWorker(void) {
    if (!ready) return false;
    atomic_store(&worker_running,true);
    Result rc=threadCreate(&worker,audio_worker,NULL,NULL,0x10000,0x2b,1);
    if (R_FAILED(rc)) rc=threadCreate(&worker,audio_worker,NULL,NULL,0x10000,0x2b,-2);
    if (R_FAILED(rc)) { atomic_store(&worker_running,false); return false; }
    rc=threadStart(&worker);
    if (R_FAILED(rc)) { atomic_store(&worker_running,false); threadClose(&worker); return false; }
    worker_started=true;
    return true;
}
void AudioSwitch_StopWorker(void) {
    if (!worker_started) return;
    atomic_store(&worker_running,false); threadWaitForExit(&worker); threadClose(&worker);
    worker_started=false;
}

bool AudioSwitch_Init(void) {
    if (R_FAILED(audoutInitialize())) return false;
    initialized=true;
    if (audoutGetSampleRate()!=48000 || audoutGetChannelCount()!=2 || audoutGetPcmFormat()!=PcmFormat_Int16) {
        AudioSwitch_Exit(); return false;
    }
    for (unsigned i=0;i<BUFFER_COUNT;++i) {
        buffers[i].buffer=memalign(4096,BUFFER_FRAMES*4);
        if (!buffers[i].buffer) { AudioSwitch_Exit(); return false; }
        buffers[i].buffer_size=BUFFER_FRAMES*4;
        queued[i]=false;
    }
    applied_volume=~0u; output_blocks=0;
    apply_volume();
    ready=R_SUCCEEDED(audoutStartAudioOut());
    if (!ready) AudioSwitch_Exit();
    return ready;
}
void AudioSwitch_Exit(void) {
    AudioSwitch_StopWorker();
    if (ready) audoutStopAudioOut();
    if (initialized) audoutExit();
    for (unsigned i=0;i<BUFFER_COUNT;++i) { free(buffers[i].buffer); buffers[i].buffer=NULL; queued[i]=false; }
    ready=false; initialized=false; remainder=0; previous[0]=previous[1]=0;
}
void AudioSwitch_Submit(const int16_t *samples,unsigned frames,unsigned rate) {
    if (!ready || !samples || !rate) return;
    AudioOutBuffer *released=NULL;
    u32 count=0;
    Result rc=0;
    /* The service returns one buffer pointer per call, not a linked list. */
    for (unsigned attempt=0;attempt<BUFFER_COUNT;++attempt) {
        released=NULL; count=0;
        rc=audoutGetReleasedAudioOutBuffer(&released,&count);
        if (R_FAILED(rc) || !count || !released) break;
        for (unsigned i=0;i<BUFFER_COUNT;++i) if (released==&buffers[i]) queued[i]=false;
    }
    unsigned depth=0;
    for (unsigned i=0;i<BUFFER_COUNT;++i) if (queued[i]) ++depth;
    if (!depth) ++starved;
    unsigned slot=BUFFER_COUNT;
    for (unsigned i=0;i<BUFFER_COUNT;++i) if (!queued[i]) { slot=i; break; }
    if (slot==BUFFER_COUNT) { ++dropped; Diagnostics_AudioQueue(depth,dropped,starved,rc); return; }
    apply_volume();
    int16_t *output=buffers[slot].buffer;
    unsigned written=0;
    /* Rate accumulator persists across blocks so packet boundaries do not
     * accumulate time drift. Linear interpolation fills the 48kHz stream. */
    for (unsigned i=0;i<frames;++i) {
        remainder+=48000;
        while (remainder>=rate) {
            remainder-=rate;
            if (written>=BUFFER_FRAMES) continue;
            unsigned fraction=remainder<48000 ? 48000-remainder : 0;
            output[written*2]=previous[0]+((int64_t)samples[i*2]-previous[0])*fraction/48000;
            output[written*2+1]=previous[1]+((int64_t)samples[i*2+1]-previous[1])*fraction/48000;
            ++written;
        }
        previous[0]=samples[i*2]; previous[1]=samples[i*2+1];
    }
    if (!written) return;
    unsigned software_percent=service_gain ? (applied_volume>100 ? applied_volume : 100) : applied_volume;
    AudioFilters_ProcessGain(output,written,software_percent);
    unsigned peak=0;
    for (unsigned i=0;i<written*2;++i) {
        int sample=output[i]; unsigned magnitude=sample<0 ? -sample : sample;
        if (magnitude>peak) peak=magnitude;
    }
    /* Publish samples before handing their memory to the audio service. */
    armDCacheFlush(output,written*4);
    if (++output_blocks%60==0) Diagnostics_Output(applied_volume,gain_numerator(applied_volume),-1.0f,0,service_gain,peak);
    buffers[slot].data_size=written*4;
    buffers[slot].data_offset=0;
    buffers[slot].next=NULL;
    rc=audoutAppendAudioOutBuffer(&buffers[slot]);
    if (R_SUCCEEDED(rc)) queued[slot]=true;
    else ++dropped;
    Diagnostics_AudioQueue(depth+(R_SUCCEEDED(rc)?1:0),dropped,starved,rc);
}
