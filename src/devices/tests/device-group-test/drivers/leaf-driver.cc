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
  const uint32_t node_1_bind_rule_1_values[] = {10, 3};
  const ddk::DeviceGroupBindRule node_1_bind_rules[] = {
      ddk::BindRuleAcceptIntList(50, node_1_bind_rule_1_values),
      ddk::BindRuleRejectBool("sandpiper", true),
  };

  const device_bind_prop_t node_1_bind_properties[] = {
      ddk::BindPropertyInt(BIND_PROTOCOL, 100),
      ddk::BindPropertyInt(BIND_USB_VID, 20),
  };

  const char* node_2_props_values_1[] = {"whimbrel", "dunlin"};
  const ddk::DeviceGroupBindRule node_2_bind_rules[] = {
      ddk::BindRuleAcceptStringList("willet", node_2_props_values_1),
      ddk::BindRuleRejectInt(20, 10),
  };

  const device_bind_prop_t node_2_bind_properties[] = {
      ddk::BindPropertyInt(BIND_PROTOCOL, 20),
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
