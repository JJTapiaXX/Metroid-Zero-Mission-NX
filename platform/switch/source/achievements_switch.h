/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NX_ACHIEVEMENTS_H
#define NX_ACHIEVEMENTS_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
void Achievements_Init(void);
void Achievements_Update(void);
void Achievements_Evaluate(void);
void Achievements_Login(void);
void Achievements_Reconnect(void);
void Achievements_Disable(void);
void Achievements_Close(void);
const char *Achievements_Status(void);
const char *Achievements_ConnectionLabel(void);
unsigned Achievements_Earned(void);
const uint32_t *Achievements_Badge(unsigned index);
unsigned Achievements_Count(void);
unsigned Achievements_Pending(void);
const char *Achievements_Title(unsigned index);
const char *Achievements_Description(unsigned index);
unsigned Achievements_Points(unsigned index);
bool Achievements_Supported(unsigned index);
bool Achievements_Unlocked(unsigned index);
size_t Achievements_ProgressSize(void);
bool Achievements_SaveProgress(uint8_t *data,size_t size);
bool Achievements_LoadProgress(const uint8_t *data,size_t size);
#endif
