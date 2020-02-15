// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"

// This is the deprecated API that has been superseded by
// zx_system_get_version_string.  It will be removed when
// users of the old ABI have all disappeared.

__EXPORT zx_status_t _zx_system_get_version(char* version, size_t len) {
  if (len < 64) {  // Legacy ABI, was never made symbolic.
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  static_assert(sizeof(DATA_CONSTANTS.version_string) >= 64);
  for (size_t i = 0; i < sizeof(DATA_CONSTANTS.version_string); ++i) {
    version[i] = DATA_CONSTANTS.version_string[i];
  }
  return ZX_OK;
}

VDSO_INTERFACE_FUNCTION(zx_system_get_version);
