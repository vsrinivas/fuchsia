// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>

#include <zxtest/zxtest.h>

zx_status_t ddk_test_bind(void* ctx, zx_device_t* parent) {
  if (RUN_ALL_TESTS(0, nullptr) != 0) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

static zx_driver_ops_t ddk_test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ddk_test_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(ddk_test, ddk_test_driver_ops, "zircon", "0.1", 1)
BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(ddk_test)
