// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/device-group-test/drivers/root-driver.h"

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <bind/fuchsia/test/cpp/fidl.h>

#include "src/devices/tests/device-group-test/drivers/root-driver-bind.h"

namespace root_driver {

zx_status_t RootDriver::Bind(void* ctx, zx_device_t* dev) {
  auto root_dev = std::make_unique<RootDriver>(dev);
  zx_status_t status = root_dev->DdkAdd(ddk::DeviceAddArgs("root"));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto ptr = root_dev.release();

  // Add a child that matches the first device group node.
  zx_device_prop_t fragment_props_1[] = {
      {50, 0, 10},
  };

  zx_device_str_prop_t str_fragment_props_1[] = {
      {"sandpiper", str_prop_bool_val(false)},
  };

  auto fragment_dev_1 = std::make_unique<RootDriver>(dev);
  status =
      fragment_dev_1->DdkAdd(ddk::DeviceAddArgs("device_group_fragment_a")
                                 .set_props(fragment_props_1)
                                 .set_str_props(str_fragment_props_1)
                                 .set_proto_id(bind::fuchsia::test::BIND_PROTOCOL_COMPAT_CHILD));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto fragment_1_ptr = fragment_dev_1.release();

  // Add the leaf device.
  zx_device_prop_t leaf_props[] = {
      {BIND_PROTOCOL, 0, bind::fuchsia::test::BIND_PROTOCOL_DEVICE},
  };

  auto leaf_dev = std::make_unique<RootDriver>(dev);
  status = leaf_dev->DdkAdd(ddk::DeviceAddArgs("leaf")
                                .set_props(leaf_props)
                                .set_proto_id(bind::fuchsia::test::BIND_PROTOCOL_DEVICE));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto leaf_ptr = leaf_dev.release();

  // Add a device that matches the other device group node.
  zx_device_str_prop_t str_fragment_props_2[] = {
      {"willet", str_prop_str_val("dunlin")},
  };

  auto fragment_dev_2 = std::make_unique<RootDriver>(dev);
  status =
      fragment_dev_2->DdkAdd(ddk::DeviceAddArgs("device_group_fragment_b")
                                 .set_str_props(str_fragment_props_2)
                                 .set_proto_id(bind::fuchsia::test::BIND_PROTOCOL_COMPAT_CHILD));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto fragment_2_ptr = fragment_dev_2.release();

  return ZX_OK;
}

void RootDriver::DdkRelease() { delete this; }

static zx_driver_ops_t root_driver_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = RootDriver::Bind;
  return ops;
}();

}  // namespace root_driver

ZIRCON_DRIVER(RootDriver, root_driver::root_driver_driver_ops, "zircon", "0.1");
