/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MZMNX_AUDIO_FILTERS_H
#define MZMNX_AUDIO_FILTERS_H
#include <stdint.h>
enum AudioOption { AUDIO_FILTER, AUDIO_TRIM, AUDIO_DC, AUDIO_LIMITER, AUDIO_PSG, AUDIO_REVERB };
unsigned AudioFilters_Get(enum AudioOption option);
void AudioFilters_Set(enum AudioOption option,unsigned value);
void AudioFilters_Defaults(void);
void AudioFilters_Load(void);
void AudioFilters_Save(void);
void AudioFilters_Reset(void);
void AudioFilters_ProcessGain(int16_t *samples,unsigned frames,unsigned percent);
void AudioFilters_Process(int16_t *samples,unsigned frames);
#endif
