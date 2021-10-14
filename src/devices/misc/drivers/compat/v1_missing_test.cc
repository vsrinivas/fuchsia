// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/driver.h>

#include "src/devices/misc/drivers/compat/v1_test_bind.h"

namespace {

constexpr zx_driver_ops_t driver_ops = [] {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(v1_missing_test, driver_ops, "zircon", "0.1");
