/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NX_SAVE_STATES_H
#define NX_SAVE_STATES_H
#include <stdbool.h>
bool SaveStates_Save(unsigned slot);
bool SaveStates_Load(unsigned slot);
bool SaveStates_Exists(unsigned slot);
const char *SaveStates_Message(void);
#endif
