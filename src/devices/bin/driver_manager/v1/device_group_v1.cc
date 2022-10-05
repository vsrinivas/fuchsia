// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v1/device_group_v1.h"

#include "src/devices/lib/log/log.h"

namespace device_group {

DeviceGroupV1::DeviceGroupV1(DeviceGroupCreateInfo create_info, DriverLoader* driver_loader)
    : DeviceGroup(std::move(create_info)), driver_loader_(driver_loader) {
  ZX_ASSERT(driver_loader_);
}

zx::status<std::optional<DeviceOrNode>> DeviceGroupV1::BindNodeImpl(
    fuchsia_driver_index::wire::MatchedDeviceGroupInfo info, const DeviceOrNode& device_or_node) {
  auto device_ptr = std::get_if<std::weak_ptr<DeviceV1Wrapper>>(&device_or_node);
  ZX_ASSERT(device_ptr);
  auto owned = device_ptr->lock();
  if (!owned) {
    LOGF(ERROR, "DeviceV1Wrapper weak_ptr not available");
    return zx::error(ZX_ERR_INTERNAL);
  }

  if (!composite_device_) {
    SetCompositeDevice(info);
  }

  auto owned_device = owned->device;
  auto status = composite_device_->BindFragment(info.node_index(), owned_device);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to BindFragment for '%.*s': %s",
         static_cast<uint32_t>(owned_device->name().size()), owned_device->name().data(),
         zx_status_get_string(status));
    return zx::error(status);
  }

  return zx::ok(std::nullopt);
}

void DeviceGroupV1::SetCompositeDevice(fuchsia_driver_index::wire::MatchedDeviceGroupInfo info) {
  ZX_ASSERT(!composite_device_);
  ZX_ASSERT(info.has_composite() && info.composite().has_driver_info() &&
            info.composite().driver_info().has_url() && info.composite().has_composite_name());
  ZX_ASSERT(info.has_node_index() && info.has_num_nodes() && info.has_node_names());

  auto node_names = std::vector<std::string>(info.node_names().count());
  for (size_t i = 0; i < info.node_names().count(); i++) {
    node_names[i] = std::string(info.node_names()[i].get());
  }

  MatchedCompositeDevice composite_info = {
      .node = info.node_index(),
      .num_nodes = info.num_nodes(),
      .name = std::string(info.composite().composite_name().get()),
      .node_names = std::move(node_names),
  };

  auto fidl_driver_info = info.composite().driver_info();
  MatchedDriverInfo matched_driver_info = {
      .driver = driver_loader_->LoadDriverUrl(std::string(fidl_driver_info.driver_url().get())),
      .colocate = fidl_driver_info.has_colocate() && fidl_driver_info.colocate(),
  };

  composite_device_ = CompositeDevice::CreateFromDriverIndex(
      MatchedCompositeDriverInfo{.composite = composite_info, .driver_info = matched_driver_info});
}

}  // namespace device_group
