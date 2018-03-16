// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t event::create(uint32_t options, event* result) {
    zx_handle_t h;
    zx_status_t status = zx_event_create(options, &h);
    if (status < 0) {
        result->reset(ZX_HANDLE_INVALID);
    } else {
        result->reset(h);
    }
    return status;
}

} // namespace zx
