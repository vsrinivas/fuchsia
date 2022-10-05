// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device_group/device_group_manager.h"

#include <utility>

#include "src/devices/lib/log/log.h"

namespace fdi = fuchsia_driver_index;
namespace fdf = fuchsia_driver_framework;

DeviceGroupManager::DeviceGroupManager(CompositeManagerBridge *bridge) : bridge_(bridge) {}

fit::result<fdf::DeviceGroupError> DeviceGroupManager::AddDeviceGroup(
    fdf::wire::DeviceGroup fidl_group, std::unique_ptr<DeviceGroup> device_group) {
  ZX_ASSERT(device_group);
  ZX_ASSERT(fidl_group.has_topological_path() && fidl_group.has_nodes() &&
            !fidl_group.nodes().empty());

  auto topological_path = std::string(fidl_group.topological_path().get());
  if (device_groups_.find(topological_path) != device_groups_.end()) {
    LOGF(ERROR, "Duplicate device group %.*s", static_cast<int>(topological_path.size()),
         topological_path.data());
    return fit::error(fdf::DeviceGroupError::kAlreadyExists);
  }

  auto node_count = fidl_group.nodes().count();
  AddToIndexCallback callback =
      [this, group = std::move(device_group), topological_path, node_count](
          zx::status<fuchsia_driver_index::DriverIndexAddDeviceGroupResponse> result) mutable {
        if (!result.is_ok()) {
          if (result.status_value() == ZX_ERR_NOT_FOUND) {
            device_groups_[topological_path] = std::move(group);
            return;
          }

          LOGF(ERROR, "DeviceGroupManager::AddDeviceGroup failed: %d", result.status_value());
          return;
        }

        if (result->node_names().size() != node_count) {
          LOGF(WARNING,
               "DriverIndexAddDeviceGroupResponse node_names count doesn't match node_count.");
          return;
        }

        device_groups_[topological_path] = std::move(group);

        // Now that there is a new device group, we can tell the bridge to attempt binds again.
        bridge_->BindNodesForDeviceGroups();
      };

  bridge_->AddDeviceGroupToDriverIndex(fidl_group, std::move(callback));
  return fit::ok();
}

zx::status<std::optional<CompositeNodeAndDriver>> DeviceGroupManager::BindDeviceGroupNode(
    fdi::wire::MatchedDeviceGroupNodeInfo match_info, const DeviceOrNode &device_or_node) {
  if (!match_info.has_device_groups() || match_info.device_groups().empty()) {
    LOGF(ERROR, "MatchedDeviceGroupNodeInfo needs to contain as least one device group");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  // Go through each device group until we find an available one with an unbound node.
  for (auto device_group_info : match_info.device_groups()) {
    if (!device_group_info.has_topological_path() || !device_group_info.has_node_index() ||
        !device_group_info.has_num_nodes() || !device_group_info.has_node_names() ||
        !device_group_info.has_composite()) {
      LOGF(WARNING, "MatchedDeviceGroupInfo missing field(s)");
      continue;
    }

    auto &topological_path = device_group_info.topological_path();
    auto &node_index = device_group_info.node_index();
    auto &num_nodes = device_group_info.num_nodes();
    auto &driver = device_group_info.composite();
    auto &node_names = device_group_info.node_names();

    if (node_index >= num_nodes) {
      LOGF(WARNING, "MatchedDeviceGroupInfo node_index is out of bounds.");
      continue;
    }

    if (node_names.count() != num_nodes) {
      LOGF(WARNING, "MatchedDeviceGroupInfo num_nodes doesn't match node_names count.");
      continue;
    }

    std::vector<std::string> node_names_vec;
    for (auto node_name : node_names) {
      node_names_vec.emplace_back(node_name.get());
    }

    auto topological_path_val = std::string(topological_path.get());
    if (device_groups_.find(topological_path_val) == device_groups_.end()) {
      LOGF(ERROR, "Missing device group %s", topological_path_val.c_str());
      continue;
    }

    if (!device_groups_[topological_path_val]) {
      LOGF(ERROR, "Stored device group in %s is null", topological_path_val.c_str());
      continue;
    }

    auto &device_group = device_groups_[topological_path_val];
    auto result = device_group->BindNode(device_group_info, device_or_node);
    if (result.is_ok()) {
      auto composite_node = result.value();
      if (composite_node.has_value() && driver.has_driver_info()) {
        return zx::ok(
            CompositeNodeAndDriver{.driver = driver.driver_info(), .node = composite_node.value()});
      }

      return zx::ok(std::nullopt);
    }

    if (result.error_value() != ZX_ERR_ALREADY_BOUND) {
      LOGF(ERROR, "Failed to bind node: %d", result.error_value());
    }
  }

  return zx::error(ZX_ERR_NOT_FOUND);
}

zx::status<std::optional<CompositeNodeAndDriver>> DeviceGroupManager::BindDeviceGroupNode(
    fdi::MatchedDeviceGroupNodeInfo match_info, const DeviceOrNode &device_or_node) {
  fidl::Arena<> arena;
  return BindDeviceGroupNode(fidl::ToWire(arena, std::move(match_info)), device_or_node);
}
