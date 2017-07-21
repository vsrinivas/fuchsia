// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"

mx_status_t _mx_channel_call(mx_handle_t handle, uint32_t options,
                             mx_time_t deadline,
                             const mx_channel_call_args_t* args,
                             uint32_t* actual_bytes,
                             uint32_t* actual_handles,
                             mx_status_t* read_status) {
    mx_status_t internal_read_status;
    mx_status_t* rd_status_p =
        read_status ? read_status : &internal_read_status;

    mx_status_t status = SYSCALL_mx_channel_call_noretry(
        handle, options, deadline, args,
        actual_bytes, actual_handles, rd_status_p);
    while (unlikely(status == MX_ERR_CALL_FAILED) &&
           unlikely(*rd_status_p == MX_ERR_INTERNAL_INTR_RETRY)) {
        status = SYSCALL_mx_channel_call_finish(
            deadline, args, actual_bytes, actual_handles, rd_status_p);
    }

    return status;
}

VDSO_INTERFACE_FUNCTION(mx_channel_call);
