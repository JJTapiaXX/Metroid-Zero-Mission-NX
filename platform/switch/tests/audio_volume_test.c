/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "audio_volume.h"
#include <stdint.h>
int overlay_test(void) {
    if (AudioVolume_Get()!=50) return 1;
    AudioVolume_Set(200);
    if (AudioVolume_Get()!=150) return 2;
    AudioVolume_Set(0);
    if (AudioVolume_Get()!=0 || AudioVolume_Mix(INT32_MAX,INT32_MAX,0)!=0) return 3;
    if (AudioVolume_Mix(10000,2000,100)!=11000) return 4;
    if (AudioVolume_Mix(10000,2000,50)!=5500) return 5;
    if (AudioVolume_Mix(-10000,-2000,100)!=-11000) return 6;
    /* Former 9-bit masking wrapped a positive overflow into negative audio. */
    if (AudioVolume_Mix(70000,0,100)!=32767) return 7;
    if (AudioVolume_Mix(-70000,0,100)!=-32768) return 8;
    if (AudioVolume_Mix(INT32_MAX,INT32_MAX,100)!=32767) return 9;
    if (AudioVolume_Mix(INT32_MIN,INT32_MIN,100)!=-32768) return 10;
    return 0;
}
