/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AUDIO_SWITCH_H
#define AUDIO_SWITCH_H
#include <stdbool.h>
#include <stdint.h>
uint64_t AudioSwitch_NowNs(void);
bool AudioSwitch_Init(void);
void AudioSwitch_Exit(void);
void AudioSwitch_Submit(const int16_t *samples,unsigned frames,unsigned rate);
bool AudioSwitch_StartWorker(void);
void AudioSwitch_StopWorker(void);
void AudioSwitch_LockState(void);
void AudioSwitch_UnlockState(void);
#endif
