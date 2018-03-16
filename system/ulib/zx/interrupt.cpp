// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/interrupt.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t interrupt::create(const resource& resource, uint32_t options, interrupt* result) {
    zx_handle_t h;
    zx_status_t status = zx_interrupt_create(resource.get(), options, &h);
    if (status < 0) {
        result->reset(ZX_HANDLE_INVALID);
    } else {
        result->reset(h);
    }
    return status;
}

} // namespace zx
