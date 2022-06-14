// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/device.h>

#include "sdk/lib/driver_test_realm/fake_driver/fake_driver-bind.h"

namespace fake_driver {

static zx_driver_ops_t fake_driver_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = [](void* ctx, zx_device_t* dev) { return ZX_OK; };
  return ops;
}();

}  // namespace fake_driver

ZIRCON_DRIVER(FakeDriver, fake_driver::fake_driver_driver_ops, "zircon", "0.1");
