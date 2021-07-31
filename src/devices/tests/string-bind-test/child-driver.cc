// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include "src/devices/tests/string-bind-test/child-driver-bind.h"

static zx_device_t* dev = nullptr;

static void unbind(void* ctx) { device_unbind_reply(dev); }

static constexpr zx_protocol_device_t dev_ops = []() {
  zx_protocol_device_t ops = {};
  ops.version = DEVICE_OPS_VERSION;
  ops.unbind = unbind;
  return ops;
}();

static zx_status_t bind(void* ctx, zx_device_t* parent) {
  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "child";
  args.ops = &dev_ops;

  return device_add(parent, &args, &dev);
}

static constexpr zx_driver_ops_t driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = bind;
  return ops;
}();

ZIRCON_DRIVER(string_bind_test_child, driver_ops, "zircon", "0.1");
