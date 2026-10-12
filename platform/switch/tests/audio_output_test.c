/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <switch.h>
#include "audio_switch.h"
#include "audio_filters.h"
#include <string.h>

void *memset(void *dest,int value,size_t count) { unsigned char *p=dest; while (count--) *p++=value; return dest; }
static _Alignas(4096) unsigned char arena[65536];
static unsigned used, output_frames, service_exits;
static bool unsupported, reject_gain;
static unsigned master=50, flush_calls, last_peak;
static float hardware_volume;
static void *flushed_buffer;
unsigned AudioVolume_Get(void) { return master; }
Result audoutSetAudioOutVolume(float volume) { if (reject_gain) return 7; hardware_volume=volume; return 0; }
Result audoutGetAudioOutVolume(float *volume) { *volume=hardware_volume; return 0; }
void armDCacheFlush(void *addr,size_t size) { (void)size; flushed_buffer=addr; ++flush_calls; }
void Diagnostics_Output(unsigned percent,unsigned gain,float actual,uint32_t result,bool hardware,unsigned peak) {
    (void)percent;(void)gain;(void)actual;(void)result;(void)hardware;(void)peak;
}
static AudioOutBuffer *released;
static int16_t input[224*2];
void mutexLock(Mutex *m) { (void)m; }
void mutexUnlock(Mutex *m) { (void)m; }
Result threadCreate(Thread *t,void (*entry)(void*),void *arg,void *stack,size_t size,int priority,int core) {
    (void)t;(void)entry;(void)arg;(void)stack;(void)size;(void)priority;(void)core; return 0;
}
Result threadStart(Thread *t) { (void)t; return 0; }
Result threadWaitForExit(Thread *t) { (void)t; return 0; }
Result threadClose(Thread *t) { (void)t; return 0; }
uint64_t armGetSystemTick(void) { return 0; }
uint64_t armTicksToNs(uint64_t t) { return t; }
void svcSleepThread(int64_t ns) { (void)ns; }
void GameSwitch_AudioTick(void) { }
void Diagnostics_AudioQueue(unsigned queued,unsigned dropped,unsigned starved,uint32_t result) {
    (void)queued;(void)dropped;(void)starved;(void)result;
}
void *memalign(size_t alignment,size_t bytes) {
    if (alignment!=4096 || bytes>sizeof(arena)-used) return NULL;
    void *p=arena+used; used+=bytes; return p;
}
void free(void *pointer) { (void)pointer; }
Result audoutInitialize(void) { return 0; }
void audoutExit(void) { ++service_exits; }
u32 audoutGetSampleRate(void) { return unsupported ? 44100 : 48000; }
u32 audoutGetChannelCount(void) { return 2; }
PcmFormat audoutGetPcmFormat(void) { return PcmFormat_Int16; }
Result audoutStartAudioOut(void) { return 0; }
Result audoutStopAudioOut(void) { return 0; }
Result audoutGetReleasedAudioOutBuffer(AudioOutBuffer **buffer,u32 *count) {
    *buffer=released; *count=released ? 1 : 0; released=NULL; return 0;
}
Result audoutAppendAudioOutBuffer(AudioOutBuffer *buffer) {
    if ((uintptr_t)buffer->buffer%4096 || buffer->data_size>buffer->buffer_size || buffer->data_size%4) return 1;
    if (buffer->buffer!=flushed_buffer) return 2;
    flushed_buffer=NULL; last_peak=0;
    int16_t *pcm=buffer->buffer;
    for (unsigned i=0;i<buffer->data_size/2;++i) {
        int value=pcm[i]; unsigned magnitude=value<0 ? -value : value;
        if (magnitude>last_peak) last_peak=magnitude;
    }
    output_frames+=buffer->data_size/4; released=buffer; return 0;
}
int overlay_test(void) {
    AudioFilters_Set(AUDIO_FILTER,0); AudioFilters_Set(AUDIO_TRIM,2);
    AudioFilters_Set(AUDIO_DC,0); AudioFilters_Set(AUDIO_LIMITER,0);
    for (unsigned i=0;i<224;++i) { input[i*2]=(int16_t)(i*97-10000); input[i*2+1]=-input[i*2]; }
    if (!AudioSwitch_Init()) return 1;
    if (!AudioSwitch_StartWorker()) return 8;
    for (unsigned i=0;i<100;++i) {
        AudioSwitch_Submit(input,224,13379);
        if (last_peak>12000) return 17;
    }
    if (output_frames!=(224u*100*48000u)/13379u) return 2;
    if (hardware_volume!=0.5f || flush_calls!=100) return 9;
    for (unsigned i=0;i<224*2;++i) input[i]=32000;
    master=1; AudioSwitch_Submit(input,224,13379);
    if (hardware_volume<0.00999f || hardware_volume>0.01001f) return 10;
    master=0; AudioSwitch_Submit(input,224,13379);
    if (hardware_volume!=0.0f) return 11;
    master=100; AudioSwitch_Submit(input,224,13379);
    if (hardware_volume!=1.0f) return 12;
    reject_gain=true;
    master=1; AudioSwitch_Submit(input,224,13379);
    if (last_peak!=320) return 13;
    master=50; AudioSwitch_Submit(input,224,13379);
    if (last_peak!=16000) return 14;
    master=0; AudioSwitch_Submit(input,224,13379);
    if (last_peak) return 15;
    reject_gain=false;
    master=150;
    for (unsigned i=0;i<224*2;++i) input[i]=20000;
    AudioSwitch_Submit(input,224,48000); AudioSwitch_Submit(input,224,48000);
    if (hardware_volume!=1.0f || last_peak!=30000) return 18;
    master=100; AudioFilters_Set(AUDIO_FILTER,2);
    for (unsigned i=0;i<224;++i) input[i*2]=input[i*2+1]=(i&1) ? -20000 : 20000;
    for (unsigned block=0;block<3;++block) AudioSwitch_Submit(input,224,48000);
    if (hardware_volume!=1.0f || last_peak>1000) return 16;
    AudioSwitch_Exit();
    if (service_exits!=1) return 3;
    AudioSwitch_Exit();
    if (service_exits!=1) return 4;
    unsupported=true;
    if (AudioSwitch_Init()) return 5;
    if (service_exits!=2) return 6;
    AudioSwitch_Exit();
    if (service_exits!=2) return 7;
    return 0;
}
