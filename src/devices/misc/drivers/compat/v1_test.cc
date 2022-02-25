// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/misc/drivers/compat/v1_test.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include "src/devices/misc/drivers/compat/v1_test_bind.h"

namespace {

zx_status_t v1_test_init(void** out_ctx) {
  zxlogf(INFO, "v1_test_init");
  // We expect the test to delete `V1Test`.
  *out_ctx = new V1Test{};
  return ZX_OK;
}

zx_status_t v1_test_bind(void* ctx, zx_device_t* dev) {
  zxlogf(INFO, "v1_test_bind");
  auto v1_test = static_cast<V1Test*>(ctx);
  v1_test->did_bind = true;
  v1_test->status = device_get_protocol(dev, 0, nullptr);
  if (v1_test->status != ZX_OK) {
    return v1_test->status;
  }
  device_add_args_t args{
      .name = "v1",
  };
  return device_add(dev, &args, &v1_test->zxdev);
}

void v1_test_release(void* ctx) {
  zxlogf(INFO, "v1_test_release");
  static_cast<V1Test*>(ctx)->did_release = true;
}

constexpr zx_driver_ops_t driver_ops = [] {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.init = v1_test_init;
  ops.bind = v1_test_bind;
  ops.release = v1_test_release;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(v1_test, driver_ops, "zircon", "0.1");
