// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/log.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t log::create(log* result, uint32_t flags) {
    mx_handle_t h = mx_log_create(flags);
    if (h < 0) {
        result->reset(MX_HANDLE_INVALID);
        return h;
    } else {
        result->reset(h);
        return NO_ERROR;
    }
}

} // namespace mx
