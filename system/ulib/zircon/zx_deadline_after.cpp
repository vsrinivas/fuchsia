// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "private.h"

zx_time_t _zx_deadline_after(zx_duration_t nanoseconds) {
    auto now = VDSO_zx_clock_get_monotonic();
    auto deadline = nanoseconds + now;
    // Check for overflow.  |nanoseconds| is unsigned, so we only get a
    // deadline in the past if overflow occurred.
    if (deadline < now)
        return ZX_TIME_INFINITE;
    return deadline;
}

VDSO_INTERFACE_FUNCTION(zx_deadline_after);
