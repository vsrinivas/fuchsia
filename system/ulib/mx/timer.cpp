// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/timer.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t timer::create(uint32_t options, uint32_t clock_id, timer* result) {
    mx_handle_t h = MX_HANDLE_INVALID;
    mx_status_t status = mx_timer_create(options, clock_id, &h);
    result->reset(h);
    return status;
}

} // namespace mx
