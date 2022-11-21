// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/node_group/node_group_manager.h"

#include <utility>

#include "src/devices/lib/log/log.h"

namespace fdi = fuchsia_driver_index;
namespace fdf = fuchsia_driver_framework;

NodeGroupManager::NodeGroupManager(CompositeManagerBridge *bridge) : bridge_(bridge) {}

fit::result<fdf::NodeGroupError> NodeGroupManager::AddNodeGroup(
    fdf::wire::NodeGroup fidl_group, std::unique_ptr<NodeGroup> node_group) {
  ZX_ASSERT(node_group);
  ZX_ASSERT(fidl_group.has_name() && fidl_group.has_nodes() && !fidl_group.nodes().empty());

  auto name = std::string(fidl_group.name().get());
  if (node_groups_.find(name) != node_groups_.end()) {
    LOGF(ERROR, "Duplicate node group %.*s", static_cast<int>(name.size()), name.data());
    return fit::error(fdf::NodeGroupError::kAlreadyExists);
  }

  auto node_count = fidl_group.nodes().count();
  AddToIndexCallback callback =
      [this, group = std::move(node_group), name, node_count](
          zx::result<fuchsia_driver_index::DriverIndexAddNodeGroupResponse> result) mutable {
        if (!result.is_ok()) {
          if (result.status_value() == ZX_ERR_NOT_FOUND) {
            node_groups_[name] = std::move(group);
            return;
          }

          LOGF(ERROR, "NodeGroupManager::AddNodeGroup failed: %d", result.status_value());
          return;
        }

        if (result->node_names().size() != node_count) {
          LOGF(WARNING,
               "DriverIndexAddNodeGroupResponse node_names count doesn't match node_count.");
          return;
        }

        node_groups_[name] = std::move(group);

        // Now that there is a new node group, we can tell the bridge to attempt binds again.
        bridge_->BindNodesForNodeGroups();
      };

  bridge_->AddNodeGroupToDriverIndex(fidl_group, std::move(callback));
  return fit::ok();
}

zx::result<std::optional<CompositeNodeAndDriver>> NodeGroupManager::BindNodeRepresentation(
    fdi::wire::MatchedNodeRepresentationInfo match_info, const DeviceOrNode &device_or_node) {
  if (!match_info.has_node_groups() || match_info.node_groups().empty()) {
    LOGF(ERROR, "MatchedNodeRepresentationInfo needs to contain as least one node group");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  // Go through each node group until we find an available one with an unbound node.
  for (auto node_group_info : match_info.node_groups()) {
    if (!node_group_info.has_name() || !node_group_info.has_node_index() ||
        !node_group_info.has_num_nodes() || !node_group_info.has_node_names() ||
        !node_group_info.has_composite()) {
      LOGF(WARNING, "MatchedNodeGroupInfo missing field(s)");
      continue;
    }

    auto &name = node_group_info.name();
    auto &node_index = node_group_info.node_index();
    auto &num_nodes = node_group_info.num_nodes();
    auto &driver = node_group_info.composite();
    auto &node_names = node_group_info.node_names();

    if (node_index >= num_nodes) {
      LOGF(WARNING, "MatchedNodeGroupInfo node_index is out of bounds.");
      continue;
    }

    if (node_names.count() != num_nodes) {
      LOGF(WARNING, "MatchedNodeGroupInfo num_nodes doesn't match node_names count.");
      continue;
    }

    std::vector<std::string> node_names_vec;
    for (auto node_name : node_names) {
      node_names_vec.emplace_back(node_name.get());
    }

    auto name_val = std::string(name.get());
    if (node_groups_.find(name_val) == node_groups_.end()) {
      LOGF(ERROR, "Missing node group %s", name_val.c_str());
      continue;
    }

    if (!node_groups_[name_val]) {
      LOGF(ERROR, "Stored node group in %s is null", name_val.c_str());
      continue;
    }

    auto &node_group = node_groups_[name_val];
    auto result = node_group->BindNode(node_group_info, device_or_node);
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

zx::result<std::optional<CompositeNodeAndDriver>> NodeGroupManager::BindNodeRepresentation(
    fdi::MatchedNodeRepresentationInfo match_info, const DeviceOrNode &device_or_node) {
  fidl::Arena<> arena;
  return BindNodeRepresentation(fidl::ToWire(arena, std::move(match_info)), device_or_node);
}
