// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device_group/device_group_manager.h"

#include <utility>

#include "src/devices/lib/log/log.h"

namespace fdi = fuchsia_driver_index;
namespace fdf = fuchsia_driver_framework;

DeviceGroupManager::DeviceGroupManager(CompositeManagerBridge *bridge) : bridge_(bridge) {}

void DeviceGroupManager::CreateDeviceGroup(CreateDeviceGroupRequestView request,
                                           CreateDeviceGroupCompleter::Sync &completer) {
  completer.Reply(AddDeviceGroup(*request));
}

fitx::result<fdf::DeviceGroupError> DeviceGroupManager::AddDeviceGroup(
    fdf::wire::DeviceGroup fidl_group) {
  if (!fidl_group.has_topological_path() || !fidl_group.has_nodes()) {
    return fitx::error(fdf::DeviceGroupError::kMissingArgs);
  }

  if (fidl_group.nodes().empty()) {
    return fitx::error(fdf::DeviceGroupError::kEmptyNodes);
  }

  auto topological_path = std::string(fidl_group.topological_path().get());
  if (device_groups_.find(topological_path) != device_groups_.end()) {
    LOGF(ERROR, "Duplicate device group %.*s", static_cast<int>(topological_path.size()),
         topological_path.data());
    return fitx::error(fdf::DeviceGroupError::kAlreadyExists);
  }

  auto node_count = fidl_group.nodes().count();
  AddToIndexCallback callback =
      [this, topological_path, node_count](
          zx::status<fuchsia_driver_index::DriverIndexAddDeviceGroupResponse> result) mutable {
        if (!result.is_ok()) {
          // If a composite driver is not available yet, we can just set this to nullptr,
          // it will be created later in BindDeviceGroupNode.
          if (result.status_value() == ZX_ERR_NOT_FOUND) {
            device_groups_[topological_path] = nullptr;
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

        // Bind the matching composite driver to the device group.
        auto status = CreateDeviceGroup(
            DeviceGroupCreateInfo({topological_path, node_count, std::move(result->node_names())}),
            std::move(result->composite_driver()));
        if (status.is_error()) {
          return;
        }

        // Now that there is a new device group, we can tell the bridge to attempt binds again.
        bridge_->BindNodesForDeviceGroups();
      };

  bridge_->AddDeviceGroupToDriverIndex(fidl_group, std::move(callback));
  return fitx::ok();
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
      LOGF(INFO, "Creating device group %s", topological_path_val.c_str());
      auto result = CreateDeviceGroup(
          DeviceGroupCreateInfo({topological_path_val, num_nodes, std::move(node_names_vec)}),
          fidl::ToNatural(driver));
      if (result.is_error()) {
        continue;
      }
    }

    auto &device_group = device_groups_[topological_path_val];
    auto result = device_group->BindNode(node_index, device_or_node);
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
    fuchsia_driver_index::MatchedDeviceGroupNodeInfo match_info,
    const DeviceOrNode &device_or_node) {
  fidl::Arena<> arena;
  return BindDeviceGroupNode(fidl::ToWire(arena, std::move(match_info)), device_or_node);
}

zx::status<> DeviceGroupManager::CreateDeviceGroup(DeviceGroupCreateInfo create_info,
                                                   fdi::MatchedCompositeInfo driver) {
  auto path = create_info.topological_path;
  if (device_groups_[path]) {
    LOGF(ERROR, "Device group %.*s is already bound to a composite driver",
         static_cast<int>(path.size()), path.data());
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }

  auto device_group = bridge_->CreateDeviceGroup(std::move(create_info), std::move(driver));
  if (!device_group.is_ok()) {
    LOGF(ERROR, "Failed to create device group: %d", device_group.status_value());
    return device_group.take_error();
  }

  if (!device_group.value()) {
    LOGF(ERROR, "Failed to create device group, DeviceGroup is null");
    return zx::error(ZX_ERR_INTERNAL);
  }

  device_groups_[path] = std::move(device_group.value());
  return zx::ok();
}
