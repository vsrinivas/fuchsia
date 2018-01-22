// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "private.h"

zx_time_t _zx_time_get(uint32_t clock_id) {
    return SYSCALL_zx_clock_get(clock_id);
}

VDSO_INTERFACE_FUNCTION(zx_time_get);
