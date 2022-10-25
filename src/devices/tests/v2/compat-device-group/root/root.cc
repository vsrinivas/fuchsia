// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header has to come first, and we define our ZX_PROTOCOL, so that
// we don't have to edit protodefs.h to add this test protocol.
#include <bind/fuchsia/compat/cpp/bind.h>
#define ZX_PROTOCOL_PARENT bind_fuchsia_compat::BIND_PROTOCOL_PARENT

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>

#include <bind/fuchsia/test/cpp/bind.h>

#include "src/devices/tests/v2/compat-device-group/root/root-bind.h"
#include "src/devices/tests/v2/compat-device-group/root/root.h"

namespace root {

zx_status_t Root::Bind(void* ctx, zx_device_t* dev) {
  auto root_dev = std::make_unique<Root>(dev);
  auto status = root_dev->DdkAdd(ddk::DeviceAddArgs("root"));
  if (status != ZX_OK) {
    return status;
  }

  const uint32_t node_1_bind_rule_1_values[] = {10, 3};
  const ddk::DeviceGroupBindRule node_1_bind_rules[] = {
      ddk::BindRuleAcceptIntList(50, node_1_bind_rule_1_values),
      ddk::BindRuleRejectBool("sandpiper", true),
  };

  const device_bind_prop_t node_1_bind_properties[] = {
      ddk::BindPropertyInt(BIND_PROTOCOL, 100),
      ddk::BindPropertyInt(BIND_USB_VID, 20),
  };

  const uint32_t node_2_props_values_1[] = {88, 99};
  const ddk::DeviceGroupBindRule node_2_bind_rules[] = {
      ddk::BindRuleAcceptIntList(BIND_PLATFORM_DEV_VID, node_2_props_values_1),
      ddk::BindRuleRejectInt(20, 10),
  };

  const device_bind_prop_t node_2_bind_properties[] = {
      ddk::BindPropertyInt(BIND_PROTOCOL, 20),
  };

  status = root_dev->DdkAddDeviceGroup(
      "device_group", ddk::DeviceGroupDesc(node_1_bind_rules, node_1_bind_properties)
                          .AddNode(node_2_bind_rules, node_2_bind_properties));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto ptr = root_dev.release();

  // Add a child that matches the first device group node.
  zx_device_prop_t node_props_1[] = {
      {50, 0, 10},
  };
  auto node_dev_1 = std::make_unique<Root>(dev);
  status = node_dev_1->DdkAdd(ddk::DeviceAddArgs("device_group_node_a")
                                  .set_props(node_props_1)
                                  .set_proto_id(bind_fuchsia_test::BIND_PROTOCOL_COMPAT_CHILD));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto node_1_ptr = node_dev_1.release();

  // Add a child that matches the other device group node.
  zx_device_prop_t node_props_2[] = {
      {BIND_PLATFORM_DEV_VID, 0, 88},
  };
  auto node_dev_2 = std::make_unique<Root>(dev);
  status = node_dev_2->DdkAdd(ddk::DeviceAddArgs("device_group_node_b")
                                  .set_props(node_props_2)
                                  .set_proto_id(bind_fuchsia_test::BIND_PROTOCOL_COMPAT_CHILD));
  if (status != ZX_OK) {
    return status;
  }
  __UNUSED auto node_2_ptr = node_dev_2.release();

  return ZX_OK;
}

void Root::DdkRelease() { delete this; }

static zx_driver_ops_t root_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Root::Bind;
  return ops;
}();

}  // namespace root

ZIRCON_DRIVER(Root, root::root_ops, "zircon", "0.1");
