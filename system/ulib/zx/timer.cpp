// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/timer.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t timer::create(uint32_t options, uint32_t clock_id, timer* result) {
    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_status_t status = zx_timer_create(options, clock_id, &h);
    result->reset(h);
    return status;
}

} // namespace zx
