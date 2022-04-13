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

  const zx_device_str_prop_val_t fragment_1_props_values_2[] = {
      str_prop_bool_val(true),
  };

  const device_group_prop_t fragment_1_props[] = {
      device_group_prop_t{
          .key = device_group_prop_int_key(50),
          .condition = DEVICE_GROUP_PROPERTY_CONDITION_ACCEPT,
          .values = fragment_1_props_values_1,
          .values_count = std::size(fragment_1_props_values_1),
      },
      device_group_prop_t{
          .key = device_group_prop_str_key("sandpiper"),
          .condition = DEVICE_GROUP_PROPERTY_CONDITION_REJECT,
          .values = fragment_1_props_values_2,
          .values_count = std::size(fragment_1_props_values_2),
      },
  };

  const device_group_fragment fragment_1{
      .name = "fragment-1",
      .props = fragment_1_props,
      .props_count = std::size(fragment_1_props),
  };

  const zx_device_str_prop_val_t fragment_2_props_values_1[] = {
      str_prop_str_val("whimbrel"),
      str_prop_str_val("dunlin"),
  };

  const zx_device_str_prop_val_t fragment_2_props_values_2[] = {
      str_prop_int_val(10),
  };

  const device_group_prop_t fragment_2_props[] = {
      device_group_prop_t{
          .key = device_group_prop_str_key("willet"),
          .condition = DEVICE_GROUP_PROPERTY_CONDITION_ACCEPT,
          .values = fragment_2_props_values_1,
          .values_count = std::size(fragment_2_props_values_1),
      },
      device_group_prop_t{
          .key = device_group_prop_int_key(20),
          .condition = DEVICE_GROUP_PROPERTY_CONDITION_REJECT,
          .values = fragment_2_props_values_2,
          .values_count = std::size(fragment_2_props_values_2),
      },
  };

  const device_group_fragment fragment_2{
      .name = "fragment-2",
      .props = fragment_2_props,
      .props_count = std::size(fragment_2_props),
  };

  device_group_fragment_t fragments[]{fragment_1, fragment_2};

  zx_device_prop_t group_props[] = {
      {BIND_USB_VID, 0, 100},
      {20, 0, 5},
  };

  const zx_device_str_prop group_str_props[] = {
      {"plover", str_prop_int_val(10)},
  };

  device_group_desc_t group_desc = {
      .fragments = fragments,
      .fragments_count = std::size(fragments),
      .props = group_props,
      .props_count = std::size(group_props),
      .str_props = group_str_props,
      .str_props_count = std::size(group_str_props),
      .spawn_colocated = true,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = dev->DdkAddDeviceGroup("device_group", &group_desc);
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
