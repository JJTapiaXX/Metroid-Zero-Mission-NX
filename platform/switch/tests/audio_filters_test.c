/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "audio_filters.h"
#include <stdint.h>
#include <stddef.h>
void *memset(void *dest,int value,size_t count) { unsigned char *p=dest; while (count--) *p++=value; return dest; }
static int16_t pcm[2048*2];
static void bypass(void) {
    AudioFilters_Set(AUDIO_FILTER,0); AudioFilters_Set(AUDIO_DC,0);
    AudioFilters_Set(AUDIO_LIMITER,0); AudioFilters_Set(AUDIO_TRIM,2);
    AudioFilters_Reset();
}
int overlay_test(void) {
    if (AudioFilters_Get(AUDIO_FILTER)!=0 || AudioFilters_Get(AUDIO_TRIM)!=2 ||
        !AudioFilters_Get(AUDIO_DC) || !AudioFilters_Get(AUDIO_LIMITER) ||
        !AudioFilters_Get(AUDIO_PSG) || AudioFilters_Get(AUDIO_REVERB)) return 1;
    bypass();
    for (unsigned i=0;i<4096;++i) pcm[i]=(i&1) ? -20000 : 20000;
    AudioFilters_Process(pcm,2048);
    if (pcm[3000]!=20000 || pcm[3001]!=-20000) return 2;
    AudioFilters_Set(AUDIO_TRIM,0);
    AudioFilters_Process(pcm,2048);
    if (pcm[3000]!=5000 || pcm[3001]!=-5000) return 3;
    bypass(); AudioFilters_Set(AUDIO_LIMITER,1);
    for (unsigned i=0;i<4096;++i) pcm[i]=(i&1) ? -32768 : 32767;
    AudioFilters_Process(pcm,2048);
    for (unsigned i=0;i<4096;++i) if (pcm[i]>24576 || pcm[i]<-24576) return 4;
    bypass(); AudioFilters_Set(AUDIO_DC,1);
    for (unsigned block=0;block<4;++block) {
        for (unsigned i=0;i<4096;++i) pcm[i]=12000;
        AudioFilters_Process(pcm,2048);
    }
    if (pcm[4094]>1 || pcm[4094]<-1) return 5;
    bypass(); AudioFilters_Set(AUDIO_FILTER,2);
    for (unsigned i=0;i<2048;++i) pcm[i*2]=pcm[i*2+1]=(i&1) ? -20000 : 20000;
    AudioFilters_Process(pcm,2048);
    if (pcm[4094]>1000 || pcm[4094]<-1000) return 6;
    /* Filtering is independent per channel and maintains state across blocks. */
    bypass(); AudioFilters_Set(AUDIO_FILTER,1);
    for (unsigned i=0;i<2048;++i) { pcm[i*2]=5000; pcm[i*2+1]=0; }
    AudioFilters_Process(pcm,2048);
    if (pcm[4094]<4900 || pcm[4095]!=0) return 7;
    bypass(); AudioFilters_Set(AUDIO_LIMITER,1);
    for (unsigned i=0;i<4096;++i) pcm[i]=(i&1) ? -30000 : 30000;
    AudioFilters_ProcessGain(pcm,2048,150);
    if (pcm[4094]<=0 || pcm[4095]>=0) return 8;
    for (unsigned i=0;i<4096;++i) if (pcm[i]>24576 || pcm[i]<-24576) return 9;
    if(AudioFilters_Get(AUDIO_SFX)!=0) return 10;
    AudioFilters_Set(AUDIO_SFX,99);
    if(AudioFilters_Get(AUDIO_SFX)!=4) return 11;
    AudioFilters_Defaults();
    if(AudioFilters_Get(AUDIO_SFX)!=0) return 12;
    return 0;
}
