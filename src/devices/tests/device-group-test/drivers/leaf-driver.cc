// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/device-group-test/drivers/leaf-driver.h"

#include "src/devices/tests/device-group-test/drivers/leaf-driver-bind.h"

namespace leaf_driver {

// static
zx_status_t LeafDriver::Bind(void* ctx, zx_device_t* device) {
  auto dev = std::make_unique<LeafDriver>(device);

  auto status = dev->DdkAdd("leaf");
  if (status != ZX_OK) {
    return status;
  }

  // Add device group.
  const device_bind_prop_value_t node_1_bind_rule_1_values[] = {
      device_bind_prop_int_val(10),
      device_bind_prop_int_val(3),
  };

  const ddk::DeviceGroupBindRule node_1_bind_rules[] = {
      ddk::DeviceGroupBindRule::AcceptList(device_bind_prop_int_key(50), node_1_bind_rule_1_values),
      ddk::DeviceGroupBindRule::RejectValue(device_bind_prop_str_key("sandpiper"),
                                            device_bind_prop_bool_val(true)),
  };

  const device_bind_prop_t node_1_bind_properties[] = {
      {
          .key = device_bind_prop_int_key(BIND_PROTOCOL),
          .value = device_bind_prop_int_val(100),
      },
      {
          .key = device_bind_prop_int_key(BIND_USB_VID),
          .value = device_bind_prop_int_val(20),
      }};

  const device_bind_prop_value_t node_2_props_values_1[] = {
      device_bind_prop_str_val("whimbrel"),
      device_bind_prop_str_val("dunlin"),
  };

  const ddk::DeviceGroupBindRule node_2_bind_rules[] = {
      ddk::DeviceGroupBindRule::AcceptList(device_bind_prop_str_key("willet"),
                                           node_2_props_values_1),
      ddk::DeviceGroupBindRule::RejectValue(device_bind_prop_int_key(20),
                                            device_bind_prop_int_val(10)),
  };

  const device_bind_prop_t node_2_bind_properties[] = {
      {
          .key = device_bind_prop_int_key(BIND_PROTOCOL),
          .value = device_bind_prop_int_val(20),
      },
  };

  status = dev->DdkAddDeviceGroup("device_group",
                                  ddk::DeviceGroupDesc(node_1_bind_rules, node_1_bind_properties)
                                      .AddNode(node_2_bind_rules, node_2_bind_properties)
                                      .set_spawn_colocated(true));
  if (status != ZX_OK) {
    return status;
  }

  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

static zx_driver_ops_t kDriverOps = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = LeafDriver::Bind;
  return ops;
}();

}  // namespace leaf_driver

ZIRCON_DRIVER(LeafDriver, leaf_driver::kDriverOps, "zircon", "0.1");
