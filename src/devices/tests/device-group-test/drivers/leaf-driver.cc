// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/tests/device-group-test/drivers/leaf-driver.h"

#include <lib/ddk/metadata.h>

#include "src/devices/tests/device-group-test/drivers/device-group-driver.h"
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

  const char* node_3_props_values_1[] = {"crow", "dunlin"};
  const ddk::DeviceGroupBindRule node_3_bind_rules[] = {
      ddk::BindRuleAcceptStringList("mockingbird", node_3_props_values_1),
      ddk::BindRuleRejectInt(20, 10),
  };

  const device_bind_prop_t node_3_bind_properties[] = {
      ddk::BindPropertyInt(BIND_PROTOCOL, 42),
  };

  const device_metadata_t metadata[] = {
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data = const_cast<char*>(device_group_driver::kMetadataStr),
          .length = strlen(device_group_driver::kMetadataStr) + 1,
      },
  };

  status = dev->DdkAddDeviceGroup("device_group_1",
                                  ddk::DeviceGroupDesc(node_1_bind_rules, node_1_bind_properties)
                                      .AddNode(node_2_bind_rules, node_2_bind_properties)
                                      .set_metadata(metadata)
                                      .set_spawn_colocated(true));
  if (status != ZX_OK) {
    return status;
  }

  status = dev->DdkAddDeviceGroup("device_group_2",
                                  ddk::DeviceGroupDesc(node_1_bind_rules, node_1_bind_properties)
                                      .AddNode(node_2_bind_rules, node_2_bind_properties)
                                      .AddNode(node_3_bind_rules, node_3_bind_properties)
                                      .set_metadata(metadata)
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
