// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>
#include <zircon/syscalls.h>

#include "private.h"

__EXPORT uint32_t _zx_system_get_page_size(void) { return DATA_CONSTANTS.page_size; }

VDSO_INTERFACE_FUNCTION(zx_system_get_page_size);
