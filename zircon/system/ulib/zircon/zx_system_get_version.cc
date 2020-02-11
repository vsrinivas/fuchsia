// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "private.h"

zx_status_t _zx_system_get_version(char* version, size_t len) {
  if (len < sizeof(DATA_CONSTANTS.version_string))
    return ZX_ERR_BUFFER_TOO_SMALL;
  for (size_t i = 0; i < sizeof(DATA_CONSTANTS.version_string); ++i)
    version[i] = DATA_CONSTANTS.version_string[i];
  return ZX_OK;
}

VDSO_INTERFACE_FUNCTION(zx_system_get_version);
