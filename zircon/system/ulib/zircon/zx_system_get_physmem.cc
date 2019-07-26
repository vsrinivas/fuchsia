// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "private.h"

uint64_t _zx_system_get_physmem(void) { return DATA_CONSTANTS.physmem; }

VDSO_INTERFACE_FUNCTION(zx_system_get_physmem);
