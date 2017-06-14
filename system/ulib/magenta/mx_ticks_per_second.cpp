// Copyright 2016, 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>

#include <magenta/compiler.h>
#include "private.h"

uint64_t _mx_ticks_per_second(void) {
    return DATA_CONSTANTS.ticks_per_second;
}

VDSO_INTERFACE_FUNCTION(mx_ticks_per_second);
