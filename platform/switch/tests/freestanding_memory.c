/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
/* Recent GCC versions may lower structure initialization to memset even for
 * freestanding code. Test executables intentionally do not link a C library. */
__attribute__((weak)) void *memset(void *destination,int value,size_t count) {
    unsigned char *p=destination;
    while (count--) *p++=(unsigned char)value;
    return destination;
}
