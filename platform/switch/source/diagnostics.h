/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MZMNX_DIAGNOSTICS_H
#define MZMNX_DIAGNOSTICS_H
#include <stdbool.h>
#include <stdint.h>
void Diagnostics_Performance(unsigned logic,unsigned ppu,unsigned ui,unsigned present);
void Diagnostics_Init(void);
void Diagnostics_Close(void);
void Diagnostics_Event(const char *message);
void Diagnostics_Frame(unsigned mode,unsigned submode,unsigned area,unsigned room,unsigned pose,unsigned x,unsigned y,bool overlay);
void Diagnostics_Audio(unsigned frames,unsigned rate,unsigned active_channels,unsigned peak,unsigned clipped);
void Diagnostics_AudioQueue(unsigned queued,unsigned dropped,unsigned starved,uint32_t result);
void Diagnostics_Output(unsigned percent,unsigned gain,float actual,uint32_t result,bool hardware,unsigned peak);
void Diagnostics_Checkpoint(unsigned phase);
#endif
