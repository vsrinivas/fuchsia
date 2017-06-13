// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtime/message.h>

#include <magenta/syscalls.h>
#include <stddef.h>

mx_status_t mxr_message_size(mx_handle_t msg_pipe,
                             uint32_t* nbytes, uint32_t* nhandles) {
    mx_status_t status = _mx_channel_read(
        msg_pipe, 0, NULL, NULL, 0, 0, nbytes, nhandles);
    if (status == MX_ERR_BUFFER_TOO_SMALL)
        status = MX_OK;
    return status;
}
