// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <zircon/compiler.h>
#include "private.h"

uint32_t _zx_system_get_num_cpus(void) { return DATA_CONSTANTS.max_num_cpus; }

VDSO_INTERFACE_FUNCTION(zx_system_get_num_cpus);
