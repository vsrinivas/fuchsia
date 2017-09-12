// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "private.h"

zx_time_t _zx_deadline_after(zx_duration_t nanoseconds) {
    return nanoseconds + VDSO_zx_time_get(ZX_CLOCK_MONOTONIC);
}

VDSO_INTERFACE_FUNCTION(zx_deadline_after);
