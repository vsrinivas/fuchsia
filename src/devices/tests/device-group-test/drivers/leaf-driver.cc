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
  const zx_device_str_prop_val_t fragment_1_props_values_1[] = {
      str_prop_int_val(10),
      str_prop_int_val(3),
  };

  const ddk::DeviceGroupProperty fragment_1_props[] = {
      ddk::DeviceGroupProperty::AcceptList(device_group_prop_int_key(50),
                                           fragment_1_props_values_1),
      ddk::DeviceGroupProperty::RejectValue(device_group_prop_str_key("sandpiper"),
                                            str_prop_bool_val(true)),
  };

  const zx_device_str_prop_val_t fragment_2_props_values_1[] = {
      str_prop_str_val("whimbrel"),
      str_prop_str_val("dunlin"),
  };

  const ddk::DeviceGroupProperty fragment_2_props[] = {
      ddk::DeviceGroupProperty::AcceptList(device_group_prop_str_key("willet"),
                                           fragment_2_props_values_1),
      ddk::DeviceGroupProperty::RejectValue(device_group_prop_int_key(20), str_prop_int_val(10)),
  };

  zx_device_prop_t group_props[] = {
      {BIND_USB_VID, 0, 100},
      {20, 0, 5},
  };

  const zx_device_str_prop group_str_props[] = {
      {"plover", str_prop_int_val(10)},
  };

  status =
      dev->DdkAddDeviceGroup("device_group", ddk::DeviceGroupDesc("fragment-1", fragment_1_props)
                                                 .AddFragment("fragment-2", fragment_2_props)
                                                 .set_props(group_props)
                                                 .set_str_props(group_str_props)
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
