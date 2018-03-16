// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/thread.h>

#include <zircon/syscalls.h>

#include <lib/zx/process.h>

namespace zx {

zx_status_t thread::create(const process& process, const char* name,
                           uint32_t name_len, uint32_t flags, thread* result) {
    zx_handle_t h;
    zx_status_t status =
        zx_thread_create(process.get(), name, name_len, flags, &h);
    if (status < 0) {
        result->reset(ZX_HANDLE_INVALID);
    } else {
        result->reset(h);
    }
    return status;
}

} // namespace zx
