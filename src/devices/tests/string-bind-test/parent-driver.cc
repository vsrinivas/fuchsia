// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <string>

#include "src/devices/tests/string-bind-test/parent-driver-bind.h"

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
  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, 3},
      {BIND_PCI_VID, 0, 1234},
      {BIND_PCI_DID, 0, 1234},
  };
  args.props = props;
  args.prop_count = countof(props);

  zx_device_str_prop_t str_props[] = {
      zx_device_str_prop_t{.key = "stringbind.lib.kinglet",
                           .property_value = str_prop_str_val("firecrest")},
      zx_device_str_prop_t{.key = "stringbind.lib.bobolink",
                           .property_value = str_prop_int_val(10)}};
  args.str_props = str_props;
  args.str_prop_count = countof(str_props);

  return device_add(parent, &args, &dev);
}

static constexpr zx_driver_ops_t driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = bind;
  return ops;
}();

ZIRCON_DRIVER(string_bind_test_parent, driver_ops, "zircon", "0.1");
