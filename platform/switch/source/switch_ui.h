/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MZM_SWITCH_UI_H
#define MZM_SWITCH_UI_H
#include "platform_switch.h"
const uint32_t *SwitchUI_Render(const SwitchInput *input);
void SwitchUI_PrepareAssets(void);
void SwitchUI_Notice(const char *message);
void SwitchUI_SetAudioAvailable(bool available);
#endif
