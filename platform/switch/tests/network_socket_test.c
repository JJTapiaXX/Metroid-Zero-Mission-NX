/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "network_socket_result.h"
int overlay_test(void) {
    if (!NetworkSocket_AttachSucceeded(-1,ENOENT)) return 1;
    if (!NetworkSocket_AttachSucceeded(4,0)) return 2;
    if (NetworkSocket_AttachSucceeded(-1,EIO)) return 3;
    if (NetworkSocket_AttachSucceeded(-1,EBADF)) return 4;
    return 0;
}
