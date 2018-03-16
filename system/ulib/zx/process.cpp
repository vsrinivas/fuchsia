// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/process.h>

#include <zircon/syscalls.h>

#include <lib/zx/job.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>

namespace zx {

zx_status_t process::create(const job& job, const char* name, uint32_t name_len, uint32_t flags,
                            process* proc, vmar* vmar) {
    zx_handle_t proc_h;
    zx_handle_t vmar_h;
    zx_status_t status = zx_process_create(job.get(), name, name_len, flags, &proc_h, &vmar_h);
    if (status < 0) {
        proc->reset(ZX_HANDLE_INVALID);
        vmar->reset(ZX_HANDLE_INVALID);
    } else {
        proc->reset(proc_h);
        vmar->reset(vmar_h);
    }
    return status;
}

zx_status_t process::start(const thread& thread_handle, uintptr_t entry,
                           uintptr_t stack, handle arg_handle,
                           uintptr_t arg2) const {
    zx_handle_t arg_h = arg_handle.release();
    zx_status_t result =
        zx_process_start(get(), thread_handle.get(), entry, stack, arg_h, arg2);
    if (result < 0)
        zx_handle_close(arg_h);
    return result;
}

} // namespace zx
