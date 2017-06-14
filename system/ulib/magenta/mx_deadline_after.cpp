// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>

#include "private.h"

mx_time_t _mx_deadline_after(mx_duration_t nanoseconds) {
    return nanoseconds + VDSO_mx_time_get(MX_CLOCK_MONOTONIC);
}

VDSO_INTERFACE_FUNCTION(mx_deadline_after);
