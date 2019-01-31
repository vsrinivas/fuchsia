// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/message.h>

#include <zircon/syscalls.h>
#include <stddef.h>

zx_status_t zxr_message_size(zx_handle_t msg_pipe,
                             uint32_t* nbytes, uint32_t* nhandles) {
    zx_status_t status = _zx_channel_read(
        msg_pipe, 0, NULL, NULL, 0, 0, nbytes, nhandles);
    if (status == ZX_ERR_BUFFER_TOO_SMALL)
        status = ZX_OK;
    return status;
}
