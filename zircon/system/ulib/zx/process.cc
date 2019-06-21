// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/process.h>

#include <zircon/syscalls.h>

#include <lib/zx/job.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>

namespace zx {

zx_status_t process::create(const job& job, const char* name, uint32_t name_len,
                            uint32_t flags, process* proc, vmar* vmar) {
    // Assume |proc|, |vmar| and |job| must refer to different containers, due
    // to strict aliasing.
    return zx_process_create(
        job.get(), name, name_len, flags, proc->reset_and_get_address(),
        vmar->reset_and_get_address());
}

zx_status_t process::start(const thread& thread_handle, uintptr_t entry,
                           uintptr_t stack, handle arg_handle,
                           uintptr_t arg2) const {
    return zx_process_start(get(), thread_handle.get(), entry, stack, arg_handle.release(), arg2);
}

zx_status_t process::get_child(uint64_t koid, zx_rights_t rights,
                               thread* result) const {
    // Assume |result| and |this| are distinct containers, due to strict
    // aliasing.
    return zx_object_get_child(
        value_, koid, rights, result->reset_and_get_address());
}

} // namespace zx
