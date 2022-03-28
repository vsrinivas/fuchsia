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
  const zx_device_prop_t fragment_props_1[] = {
      {50, 0, 10},
  };

  const zx_device_str_prop fragment_str_props_1[] = {
      {"sandpiper", str_prop_bool_val(false)},
  };

  const device_group_fragment fragment_1{
      .name = "fragment-1",
      .props = fragment_props_1,
      .props_count = std::size(fragment_props_1),
      .str_props = fragment_str_props_1,
      .str_props_count = std::size(fragment_str_props_1),
  };

  const zx_device_str_prop fragment_str_props_2[] = {
      {"willet", str_prop_str_val("dunlin")},
  };

  const device_group_fragment fragment_2{
      .name = "fragment-2",
      .str_props = fragment_str_props_2,
      .str_props_count = std::size(fragment_str_props_2),
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
