/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MZM_CACHE_OUNCE_H
#define MZM_CACHE_OUNCE_H
#include <stdbool.h>
#include <stddef.h>
bool CacheOunce_Init(void);
bool CacheOunce_Queue(const char *path,const void *prefix,size_t prefix_size,
                      const void *data,size_t size,bool keep_backup);
void CacheOunce_Flush(void);
bool CacheOunce_RecordRetryNeeded(void);
void CacheOunce_Close(void);
#endif
