// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/thread.h>

#include <magenta/syscalls.h>

#include <mx/process.h>

namespace mx {

mx_status_t thread::create(const process& process,
                           const char* name, uint32_t name_len,
                           uint32_t flags, thread* result) {
    mx_handle_t h;
    mx_status_t status = mx_thread_create(process.get(), name, name_len, flags, &h);
    if (status < 0) {
        result->reset(MX_HANDLE_INVALID);
    } else {
        result->reset(h);
    }
    return status;
}

} // namespace mx
