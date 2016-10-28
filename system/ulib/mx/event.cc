// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/event.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t event::create(uint32_t options, event* result) {
    mx_handle_t h;
    mx_status_t status = mx_event_create(options, &h);
    if (status < 0) {
        result->reset(MX_HANDLE_INVALID);
    } else {
        result->reset(h);
    }
    return status;
}

} // namespace mx
