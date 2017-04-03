// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>

#include "private.h"

mx_time_t _mx_deadline_after(mx_duration_t nanoseconds) {
    // TODO(teisenbe): Once we switch all syscalls to taking deadlines,
    // replace this with a real implementation.
    return nanoseconds;
}

__typeof(mx_deadline_after) mx_deadline_after
    __attribute__((weak, alias("_mx_deadline_after")));
