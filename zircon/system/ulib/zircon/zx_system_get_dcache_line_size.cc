// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <zircon/compiler.h>
#include "private.h"

uint32_t _zx_system_get_dcache_line_size(void) {
    return DATA_CONSTANTS.dcache_line_size;
}

VDSO_INTERFACE_FUNCTION(zx_system_get_dcache_line_size);
