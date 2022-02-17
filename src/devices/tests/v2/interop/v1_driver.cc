// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <bind/fuchsia/test/cpp/fidl.h>

#include "src/devices/tests/v2/interop/v1.bind.h"

namespace {

zx_status_t v1_bind(void* ctx, zx_device_t* dev) {
  zxlogf(INFO, "v1_bind");
  zx_status_t status = device_get_protocol(dev, 0, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  device_add_args_t args{
      .name = "leaf",
      .prop_count = 0,
      .proto_id = bind::fuchsia::test::BIND_PROTOCOL_DEVICE,
  };
  zx_device_t* out = nullptr;
  return device_add(dev, &args, &out);
}

constexpr zx_driver_ops_t driver_ops = [] {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = v1_bind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(v1, driver_ops, "zircon", "0.1");
