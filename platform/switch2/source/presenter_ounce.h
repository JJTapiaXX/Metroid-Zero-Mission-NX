/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MZM_PRESENTER_OUNCE_H
#define MZM_PRESENTER_OUNCE_H
#include <stdbool.h>
#include <stdint.h>
bool PresenterOunce_Init(void);
bool PresenterOunce_Warmup(unsigned milliseconds);
uint32_t *PresenterOunce_Begin(uint32_t *stride_bytes);
bool PresenterOunce_End(void);
void PresenterOunce_Close(void);
#endif
