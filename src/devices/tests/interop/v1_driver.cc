// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <bind/fuchsia/test/cpp/fidl.h>

#include "src/devices/tests/interop/v1.bind.h"

namespace {

zx_status_t v1_bind(void* ctx, zx_device_t* dev) {
  zxlogf(INFO, "v1_bind");
  zx_device_prop_t prop{
      .id = BIND_PROTOCOL,
      .value = bind::fuchsia::test::BIND_PROTOCOL_DEVICE,
  };
  device_add_args_t args{
      .name = "leaf",
      .props = &prop,
      .prop_count = 1,
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
