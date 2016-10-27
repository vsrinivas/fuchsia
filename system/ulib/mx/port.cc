// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/port.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t port::create(uint32_t options, port* result) {
    mx_handle_t h = mx_port_create(options);
    if (h < 0) {
        result->reset(MX_HANDLE_INVALID);
        return h;
    } else {
        result->reset(h);
        return NO_ERROR;
    }
}

} // namespace mx
