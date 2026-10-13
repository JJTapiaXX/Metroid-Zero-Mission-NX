/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NX_UPDATER_H
#define NX_UPDATER_H
void Updater_Init(const char *path);
void Updater_Action(void);
const char *Updater_Status(void);
const char *Updater_Button(void);
void Updater_Close(void);
#endif
