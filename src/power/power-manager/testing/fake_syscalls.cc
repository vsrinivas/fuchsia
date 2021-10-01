// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains fake syscall implementation(s) used for testing power-manager.

#include <zircon/errors.h>
#include <zircon/syscalls.h>

__EXPORT zx_status_t zx_system_set_performance_info(zx_handle_t, uint32_t, const void*, size_t) {
  return ZX_ERR_BAD_HANDLE;
}
