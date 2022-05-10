// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/driver.h>
#include <stdio.h>
#include <zircon/compiler.h>

#include "lib/ddk/debug.h"

// Enough of the DDK to let things link and be useful.

zx_driver_rec_t __zircon_driver_rec__ __EXPORT = {};

bool driver_log_severity_enabled_internal(const zx_driver_t* drv, fx_log_severity_t flag) {
  return true;
}

void driver_logf_internal(const zx_driver_t* drv, fx_log_severity_t flag, const char* tag,
                          const char* file, int line, const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  printf("[%s:%d] ", file, line);
  vprintf(msg, ap);
  printf("\n");
  va_end(ap);
}

zx_status_t device_get_variable(zx_device_t* device, const char* name, char* out, size_t out_size,
                                size_t* size_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}
