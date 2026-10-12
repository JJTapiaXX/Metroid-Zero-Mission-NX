/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NX_NETWORK_SOCKET_RESULT_H
#define NX_NETWORK_SOCKET_RESULT_H
#include <stdbool.h>
#include <errno.h>
/* libnx returns -1/ENOENT when attachment succeeds without a new fd. */
static inline bool NetworkSocket_AttachSucceeded(int descriptor,int error) {
    return descriptor>=0 || error==ENOENT;
}
#endif
