/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AUDIO_VOLUME_H
#define AUDIO_VOLUME_H
#include <stdint.h>
unsigned AudioVolume_Get(void);
void AudioVolume_Set(unsigned percent);
void AudioVolume_Load(void);
void AudioVolume_Save(void);
int16_t AudioVolume_Mix(int32_t pcm_mix, int psg_sample, unsigned percent);
#endif
