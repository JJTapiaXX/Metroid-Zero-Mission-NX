/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "audio_filters.h"
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
/* Normal levels: no low-pass, unity trim, DC removal, limiter, PSG on. */
enum { DEFAULT_SETTINGS=(2u<<2)|(1u<<4)|(1u<<5)|(1u<<6), PEAK_CEILING=24576 };
static atomic_uint settings=DEFAULT_SETTINGS;
void AudioFilters_Defaults(void) { atomic_store(&settings,DEFAULT_SETTINGS); }
static const unsigned shifts[]={0,2,4,5,6,7};
static const unsigned masks[]={3,3,1,1,1,1};
static int64_t dc_input[2],dc_output[2],low[2],low2[2];
static int32_t scratch[2048*2];
static unsigned previous_settings=~0u,limiter_gain=65536;
unsigned AudioFilters_Get(enum AudioOption option) {
    if ((unsigned)option>5) return 0;
    return (atomic_load(&settings)>>shifts[option])&masks[option];
}
void AudioFilters_Set(enum AudioOption option,unsigned value) {
    if ((unsigned)option>5) return;
    unsigned maximum=option==AUDIO_FILTER || option==AUDIO_TRIM ? 2 : 1;
    if (value>maximum) value=maximum;
    unsigned old=atomic_load(&settings),desired;
    do { desired=(old&~(masks[option]<<shifts[option]))|(value<<shifts[option]); }
    while (!atomic_compare_exchange_weak(&settings,&old,desired));
}
static bool load(const char *path) {
    unsigned char data[6]; FILE *file=fopen(path,"rb");
    if (!file) return false;
    size_t count=fread(data,1,6,file); fclose(file);
    if (count!=6 || memcmp(data,"NXA",3) || (data[3]!=1 && data[3]!=2) || data[5]!=(data[4]^0xa5) ||
        (data[4]&3)>2 || ((data[4]>>2)&3)>2) return false;
    atomic_store(&settings,data[3]==1 ? DEFAULT_SETTINGS : data[4]); return true;
}
void AudioFilters_Load(void) { if (!load("mzmnx-audio.cfg")) load("mzmnx-audio.cfg.bak"); }
void AudioFilters_Save(void) {
    unsigned char value=atomic_load(&settings);
    unsigned char data[6]={'N','X','A',2,value,value^0xa5};
    FILE *file=fopen("mzmnx-audio.cfg.tmp","wb"); if (!file) return;
    size_t count=fwrite(data,1,6,file); int result=fflush(file);
    if (fclose(file) || result || count!=6) return;
    remove("mzmnx-audio.cfg.bak"); rename("mzmnx-audio.cfg","mzmnx-audio.cfg.bak");
    rename("mzmnx-audio.cfg.tmp","mzmnx-audio.cfg");
}
void AudioFilters_Reset(void) {
    memset(dc_input,0,sizeof(dc_input)); memset(dc_output,0,sizeof(dc_output));
    memset(low,0,sizeof(low)); memset(low2,0,sizeof(low2)); limiter_gain=65536;
}
void AudioFilters_Process(int16_t *samples,unsigned frames) { AudioFilters_ProcessGain(samples,frames,100); }
void AudioFilters_ProcessGain(int16_t *samples,unsigned frames,unsigned percent) {
    if (percent>150) percent=150;
    if (!samples || frames>2048) return;
    unsigned config=atomic_load(&settings);
    if (config!=previous_settings) { AudioFilters_Reset(); previous_settings=config; }
    unsigned filter=config&3,trim=(config>>2)&3;
    unsigned level=trim==0 ? 25 : trim==1 ? 50 : 100;
    unsigned peak=0;
    for (unsigned i=0;i<frames*2;++i) {
        unsigned side=i&1;
        int64_t value=(int64_t)samples[i]*65536*percent/100;
        if (config&(1u<<4)) {
            int64_t output=value-dc_input[side]+dc_output[side]*32690/32768;
            dc_input[side]=value; dc_output[side]=output; value=output;
        }
        if (filter) {
            unsigned alpha=filter==1 ? 26700 : 11680;
            low[side]+=(value-low[side])*alpha/65536; value=low[side];
            if (filter==2) { low2[side]+=(value-low2[side])*alpha/65536; value=low2[side]; }
        }
        int32_t result=value*level/(65536*100);
        scratch[i]=result; unsigned magnitude=result<0 ? -result : result;
        if (magnitude>peak) peak=magnitude;
    }
    unsigned target=65536;
    if ((config&(1u<<5)) && peak>PEAK_CEILING) target=(PEAK_CEILING*65536u)/peak;
    if (!(config&(1u<<5)) || target<limiter_gain) limiter_gain=target;
    else limiter_gain+=(target-limiter_gain+31)/32;
    for (unsigned i=0;i<frames*2;++i) {
        int64_t value=(int64_t)scratch[i]*limiter_gain/65536;
        if (value>32767) value=32767;
        if (value < -32768) value=-32768;
        samples[i]=value;
    }
}
