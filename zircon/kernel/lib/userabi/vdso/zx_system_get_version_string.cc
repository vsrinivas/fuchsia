// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "private.h"

__EXPORT zx_string_view_t _zx_system_get_version_string() {
  return {DATA_CONSTANTS.version_string, DATA_CONSTANTS.version_string_len};
}

VDSO_INTERFACE_FUNCTION(zx_system_get_version_string);
