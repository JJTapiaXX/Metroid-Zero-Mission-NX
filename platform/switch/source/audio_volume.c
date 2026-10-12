/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "audio_volume.h"
#include <stdio.h>

static unsigned volume = 50;
unsigned AudioVolume_Get(void) { return __atomic_load_n(&volume,__ATOMIC_RELAXED); }
void AudioVolume_Set(unsigned percent) { __atomic_store_n(&volume,percent>150 ? 150 : percent,__ATOMIC_RELAXED); }
static int load_file(const char *path) {
    unsigned char data[6];
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    size_t count = fread(data, 1, sizeof(data), file);
    fclose(file);
    if (count == sizeof(data) && data[0]=='N' && data[1]=='X' &&
        data[2]=='V' && data[3]==1 && data[4]<=150 && data[5]==(data[4]^0x5a))
    {
        AudioVolume_Set(data[4]); return 1;
    }
    return 0;
}
void AudioVolume_Load(void) {
    if (!load_file("mzmnx-volume.cfg")) load_file("mzmnx-volume.cfg.bak");
}
void AudioVolume_Save(void) {
    unsigned percent=AudioVolume_Get();
    unsigned char data[6] = {'N','X','V',1,(unsigned char)percent,(unsigned char)(percent^0x5a)};
    FILE *file = fopen("mzmnx-volume.cfg.tmp", "wb");
    if (!file) return;
    size_t count = fwrite(data, 1, sizeof(data), file);
    int result = fflush(file);
    if (fclose(file)!=0 || result!=0 || count!=sizeof(data)) return;
    /* Switch SD rename does not replace an existing destination. */
    remove("mzmnx-volume.cfg.bak");
    rename("mzmnx-volume.cfg", "mzmnx-volume.cfg.bak");
    rename("mzmnx-volume.cfg.tmp", "mzmnx-volume.cfg");
}
int16_t AudioVolume_Mix(int32_t pcm_mix, int psg_sample, unsigned percent) {
    if (percent>150) percent=150;
    /* Mix at full precision, apply headroom and master gain before clipping.
     * Folding a wide accumulator into a 9-bit lookup index can reverse the
     * waveform's sign on overflow; quantizing to 8-bit also adds distortion. */
    int64_t sample=((int64_t)pcm_mix*2+psg_sample)*percent/200;
    if (sample>32767) sample=32767;
    if (sample < -32768) sample=-32768;
    return (int16_t)sample;
}
