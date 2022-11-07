// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/device-group-test/drivers/root-driver.h"

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <bind/device/group/test/lib/cpp/bind.h>
#include <bind/fuchsia/test/cpp/bind.h>

#include "src/devices/tests/device-group-test/drivers/root-driver-bind.h"

namespace bind_test = bind_device_group_test_lib;

namespace root_driver {

zx_status_t RootDriver::Bind(void* ctx, zx_device_t* dev) {
  auto root_dev = std::make_unique<RootDriver>(dev);
  zx_status_t status = root_dev->DdkAdd(ddk::DeviceAddArgs("root"));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto ptr = root_dev.release();

  // Add 2 children that matches the first device group node.
  zx_device_prop_t fragment_props_1[] = {
      {50, 0, 10},
  };

  zx_device_str_prop_t str_fragment_props_1[] = {
      {bind_test::FLAG.c_str(), str_prop_bool_val(false)},
  };

  auto fragment_dev_a_1 = std::make_unique<RootDriver>(dev);
  status =
      fragment_dev_a_1->DdkAdd(ddk::DeviceAddArgs("device_group_fragment_a_1")
                                   .set_props(fragment_props_1)
                                   .set_str_props(str_fragment_props_1)
                                   .set_proto_id(bind_fuchsia_test::BIND_PROTOCOL_COMPAT_CHILD));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto fragment_a_1_ptr = fragment_dev_a_1.release();

  auto fragment_dev_a_2 = std::make_unique<RootDriver>(dev);
  status =
      fragment_dev_a_2->DdkAdd(ddk::DeviceAddArgs("device_group_fragment_a_2")
                                   .set_props(fragment_props_1)
                                   .set_str_props(str_fragment_props_1)
                                   .set_proto_id(bind_fuchsia_test::BIND_PROTOCOL_COMPAT_CHILD));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto fragment_a_2_ptr = fragment_dev_a_2.release();

  // Add the leaf device.
  zx_device_prop_t leaf_props[] = {
      {BIND_PROTOCOL, 0, bind_fuchsia_test::BIND_PROTOCOL_DEVICE},
  };

  auto leaf_dev = std::make_unique<RootDriver>(dev);
  status = leaf_dev->DdkAdd(ddk::DeviceAddArgs("leaf")
                                .set_props(leaf_props)
                                .set_proto_id(bind_fuchsia_test::BIND_PROTOCOL_DEVICE));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto leaf_ptr = leaf_dev.release();

  // Add 2 devices that matches the other device group node.
  zx_device_str_prop_t str_fragment_props_2[] = {
      {bind_test::TEST_PROP.c_str(), str_prop_str_val(bind_test::TEST_PROP_VALUE_2.c_str())},
  };

  auto fragment_dev_b_1 = std::make_unique<RootDriver>(dev);
  status =
      fragment_dev_b_1->DdkAdd(ddk::DeviceAddArgs("device_group_fragment_b_1")
                                   .set_str_props(str_fragment_props_2)
                                   .set_proto_id(bind_fuchsia_test::BIND_PROTOCOL_COMPAT_CHILD));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto fragment_b_1_ptr = fragment_dev_b_1.release();

  auto fragment_dev_b_2 = std::make_unique<RootDriver>(dev);
  status =
      fragment_dev_b_2->DdkAdd(ddk::DeviceAddArgs("device_group_fragment_b_2")
                                   .set_str_props(str_fragment_props_2)
                                   .set_proto_id(bind_fuchsia_test::BIND_PROTOCOL_COMPAT_CHILD));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto fragment_b_2_ptr = fragment_dev_b_2.release();

  // Add a third device that matches the optional device group node.
  zx_device_str_prop_t str_fragment_props_3[] = {
      {bind_test::TEST_PROP.c_str(), str_prop_str_val(bind_test::TEST_PROP_VALUE_3.c_str())},
  };

  auto fragment_dev_c_2 = std::make_unique<RootDriver>(dev);
  status =
      fragment_dev_c_2->DdkAdd(ddk::DeviceAddArgs("device_group_fragment_c_2")
                                   .set_str_props(str_fragment_props_3)
                                   .set_proto_id(bind_fuchsia_test::BIND_PROTOCOL_COMPAT_CHILD));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto fragment_c_2_ptr = fragment_dev_c_2.release();

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
