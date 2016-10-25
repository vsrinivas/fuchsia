// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/thread.h>

#include <magenta/syscalls.h>

#include <mx/process.h>

namespace mx {

mx_status_t thread::create(thread* result, const process& process,
                           const char* name, uint32_t name_len,
                           uint32_t flags) {
    mx_handle_t h = mx_thread_create(process.get(), name, name_len, flags);
    if (h < 0) {
        result->reset(MX_HANDLE_INVALID);
        return h;
    } else {
        result->reset(h);
        return NO_ERROR;
    }
}

} // namespace mx
