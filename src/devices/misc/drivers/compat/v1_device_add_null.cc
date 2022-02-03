// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/driver.h>

#include "src/devices/misc/drivers/compat/v1_test_bind.h"

namespace {

zx_status_t v1_add_null_bind(void* ctx, zx_device_t* parent) {
  device_add_args_t args{
      .name = "v1-add-null",
  };

  return device_add(parent, &args, nullptr);
}

constexpr zx_driver_ops_t driver_ops = [] {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = v1_add_null_bind;
  return ops;
}();

}  // namespace

ZIRCON_DRIVER(v1_device_add_null_test, driver_ops, "zircon", "0.1");
