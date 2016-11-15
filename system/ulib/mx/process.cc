// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/process.h>

#include <magenta/syscalls.h>

#include <mx/job.h>
#include <mx/thread.h>

namespace mx {

mx_status_t process::create(const job& job, const char* name, uint32_t name_len, uint32_t flags,
                            process* result) {
    mx_handle_t proc_h;
    mx_handle_t vmar_h;
    mx_status_t status = mx_process_create(job.get(), name, name_len, flags, &proc_h, &vmar_h);
    if (status < 0) {
        result->reset(MX_HANDLE_INVALID);
    } else {
        result->reset(proc_h);
    }
    // TODO(teisenbe): Hold on to vmar_h instead of just closing it
    mx_handle_close(vmar_h);
    return status;
}

mx_status_t process::start(const thread& thread_handle, uintptr_t entry,
                           uintptr_t stack, handle arg_handle,
                           uintptr_t arg2) const {
    mx_handle_t arg_h = arg_handle.release();
    mx_status_t result =
        mx_process_start(get(), thread_handle.get(), entry, stack, arg_h, arg2);
    if (result < 0)
        mx_handle_close(arg_h);
    return result;
}

} // namespace mx
