/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NX_NETWORK_H
#define NX_NETWORK_H
#include <stdbool.h>
#include <stddef.h>
size_t NetworkSwitch_ResponseSize(void);
const char *NetworkSwitch_Error(void);
bool NetworkSwitch_Init(void);
bool NetworkSwitch_Request(const char *url,const char *post);
bool NetworkSwitch_Poll(char **response,int *status);
void NetworkSwitch_Close(void);
#endif
