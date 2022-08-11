// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v1/device_group_v1.h"

#include "src/devices/lib/log/log.h"

namespace device_group {

zx::status<std::unique_ptr<DeviceGroupV1>> DeviceGroupV1::Create(
    fuchsia_driver_framework::wire::DeviceGroup group,
    fuchsia_driver_index::MatchedCompositeInfo composite, Coordinator* coordinator) {
  if (!coordinator) {
    LOGF(ERROR, "Coordinator should not be null");
    return zx::error(ZX_ERR_INTERNAL);
  }

  if (!group.has_nodes() || group.nodes().empty()) {
    LOGF(ERROR, "Device group must contain nodes");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (!composite.driver_info().has_value()) {
    LOGF(ERROR, "Composite driver url missing driver info");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  auto driver_info = composite.driver_info().value();
  if (!driver_info.driver_url().has_value() || driver_info.driver_url().value().empty()) {
    LOGF(ERROR, "Composite driver url is empty");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  MatchedDriverInfo matched_driver_info = {
      .driver = coordinator->driver_loader().LoadDriverUrl(driver_info.driver_url().value()),
      .colocate = driver_info.colocate().has_value() && driver_info.colocate().value(),
  };

  MatchedCompositeDevice matched_device = {};
  if (composite.num_nodes().has_value()) {
    matched_device.num_nodes = composite.num_nodes().value();
  }

  if (composite.composite_name().has_value()) {
    matched_device.name = composite.composite_name().value();
  }

  if (composite.node_names().has_value()) {
    matched_device.node_names = composite.node_names().value();
  }

  auto composite_dev = CompositeDevice::CreateFromDriverIndex(
      MatchedCompositeDriverInfo{.composite = matched_device, .driver_info = matched_driver_info});
  return zx::ok(std::make_unique<DeviceGroupV1>(group, std::move(composite_dev)));
}

DeviceGroupV1::DeviceGroupV1(fuchsia_driver_framework::wire::DeviceGroup group,
                             std::unique_ptr<CompositeDevice> composite_device)
    : DeviceGroup(group), composite_device_(std::move(composite_device)) {}

zx::status<> DeviceGroupV1::BindNodeToComposite(uint32_t node_index, DeviceOrNode device_wrapper) {
  auto device_ptr = std::get_if<std::weak_ptr<DeviceV1Wrapper>>(&device_wrapper);
  ZX_ASSERT(device_ptr);

  auto owned = device_ptr->lock();
  if (!owned) {
    LOGF(ERROR, "DeviceV1Wrapper weak_ptr not available");
    return zx::error(ZX_ERR_INTERNAL);
  }

  auto device = owned->device;
  auto status = composite_device_->BindFragment(node_index, device);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to BindFragment for '%.*s': %s",
         static_cast<uint32_t>(device->name().size()), device->name().data(),
         zx_status_get_string(status));
    return zx::error(status);
  }

  return zx::ok();
}

}  // namespace device_group
