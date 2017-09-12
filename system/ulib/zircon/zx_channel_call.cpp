// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"

zx_status_t _zx_channel_call(zx_handle_t handle, uint32_t options,
                             zx_time_t deadline,
                             const zx_channel_call_args_t* args,
                             uint32_t* actual_bytes,
                             uint32_t* actual_handles,
                             zx_status_t* read_status) {
    zx_status_t internal_read_status;
    zx_status_t* rd_status_p =
        read_status ? read_status : &internal_read_status;

    zx_status_t status = SYSCALL_zx_channel_call_noretry(
        handle, options, deadline, args,
        actual_bytes, actual_handles, rd_status_p);
    while (unlikely(status == ZX_ERR_CALL_FAILED) &&
           unlikely(*rd_status_p == ZX_ERR_INTERNAL_INTR_RETRY)) {
        status = SYSCALL_zx_channel_call_finish(
            deadline, args, actual_bytes, actual_handles, rd_status_p);
    }

    return status;
}

VDSO_INTERFACE_FUNCTION(zx_channel_call);
