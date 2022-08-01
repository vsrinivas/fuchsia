// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device_group_manager.h"

#include "src/devices/lib/log/log.h"

namespace fdi = fuchsia_driver_index;
namespace fdf = fuchsia_driver_framework;

DeviceGroupManager::DeviceGroupManager(CompositeManagerBridge* bridge) : bridge_(bridge) {}

zx::status<> DeviceGroupManager::AddDeviceGroup(fdf::wire::DeviceGroup fidl_group) {
  if (!fidl_group.has_topological_path() || !fidl_group.has_nodes() || fidl_group.nodes().empty()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  auto topological_path = fidl_group.topological_path().get();
  if (device_groups_.find(topological_path) != device_groups_.end()) {
    LOGF(ERROR, "Duplicate device group %.*s", static_cast<int>(topological_path.size()),
         topological_path.data());
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  auto result = bridge_->AddDeviceGroupToDriverIndex(fidl_group);
  if (!result.is_ok()) {
    // Return ZX_OK if a composite driver is not available.
    if (result.status_value() == ZX_ERR_NOT_FOUND) {
      return zx::ok();
    }

    LOGF(ERROR, "DeviceGroupManager::AddDeviceGroup failed: %d", result.status_value());
    return result.take_error();
  }

  // Bind the matching composite driver to the device group.
  return BindAndCreateDeviceGroup(fidl_group, result.value());
}

zx::status<> DeviceGroupManager::BindAndCreateDeviceGroup(fdf::wire::DeviceGroup fidl_group,
                                                          fdi::wire::MatchedCompositeInfo driver) {
  if (!fidl_group.has_topological_path() || !fidl_group.has_nodes() || fidl_group.nodes().empty()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  auto topological_path = fidl_group.topological_path().get();
  if (device_groups_[topological_path]) {
    LOGF(ERROR, "Device group %.*s is already bound to a composite driver",
         static_cast<int>(topological_path.size()), topological_path.data());
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }

  auto device_group = bridge_->CreateDeviceGroup(fidl_group, driver);
  if (!device_group.is_ok()) {
    LOGF(ERROR, "Failed to create device group: %d", device_group.status_value());
    return device_group.take_error();
  }

  if (!device_group.value()) {
    LOGF(ERROR, "Failed to create device group, DeviceGroup is null");
    return zx::error(ZX_ERR_INTERNAL);
  }

  device_groups_[topological_path] = std::move(device_group.value());
  bridge_->MatchAndBindAllNodes();
  return zx::ok();
}

zx::status<> DeviceGroupManager::BindDeviceGroupNode(MatchedDeviceGroupNodeInfo match_info,
                                                     DeviceOrNode node) {
  // Go through each device group until we find an available one with an unbound node.
  for (auto device_group_info : match_info.groups) {
    auto& device_group = device_groups_[device_group_info.topological_path];
    if (!device_group) {
      LOGF(ERROR, "Missing device group: %d", ZX_ERR_INTERNAL);
      continue;
    }

    auto result = device_group->BindNode(device_group_info.node_index, node);
    if (result.is_ok()) {
      return zx::ok();
    }

    if (result.error_value() != ZX_ERR_ALREADY_BOUND) {
      LOGF(ERROR, "Failed to bind node: %d", result.error_value());
    }
  }

  return zx::error(ZX_ERR_NOT_FOUND);
}
