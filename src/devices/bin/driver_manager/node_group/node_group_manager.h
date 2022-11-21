// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_NODE_GROUP_NODE_GROUP_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_NODE_GROUP_NODE_GROUP_MANAGER_H_

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.driver.index/cpp/fidl.h>
#include <lib/zx/result.h>

#include <unordered_map>

#include "src/devices/bin/driver_manager/node_group/composite_manager_bridge.h"

struct CompositeNodeAndDriver {
  fuchsia_driver_index::wire::MatchedDriverInfo driver;
  DeviceOrNode node;
};

// This class is responsible for managing node groups. It keeps track of the device
// groups and its matching composite driver and nodes. NodeGroupManager is owned by a
// CompositeManagerBridge and must be outlived by it.
class NodeGroupManager {
 public:
  using NodeGroupMap = std::unordered_map<std::string, std::unique_ptr<NodeGroup>>;

  explicit NodeGroupManager(CompositeManagerBridge* bridge);

  // Adds a node group to the driver index. If it's successfully added, then the
  // NodeGroupManager stores the node group in a map. After that, it sends a call to
  // CompositeManagerBridge to bind all unbound devices.
  fit::result<fuchsia_driver_framework::NodeGroupError> AddNodeGroup(
      fuchsia_driver_framework::wire::NodeGroup group_info, std::unique_ptr<NodeGroup> node_group);

  // Binds the device to one of the node group nodes that it was matched to.
  // NodeGroupManager will go through the list of node groups until it finds one with
  // the node unbound.
  // DFv1:
  // This will internally use node_group_v1, which itself uses
  // CompositeDevice's BindFragment to do all the work needed to track the composite fragments
  // and to start the driver.
  // A std::nullopt is always returned.
  // DFv2:
  // This will use node_group_v2, which internally tracks the nodes in a ParentSetCollector,
  // when the parent set is completed, a child node is created that is parented by all the nodes
  // from the parent set.
  // A std::nullopt is returned if the chosen node group is not yet complete, otherwise a
  // shared pointer to the newly created child node is returned along with the driver of the
  // chosen match. DriverRunner is responsible for starting the driver on the node.
  zx::result<std::optional<CompositeNodeAndDriver>> BindNodeRepresentation(
      fuchsia_driver_index::wire::MatchedNodeRepresentationInfo match_info,
      const DeviceOrNode& device_or_node);

  // Reason for both versions of this method is that in DFv1 the match info is stored
  // via natural types because BindNodeRepresentation is outside of the fidl wire response's scope.
  // In DFv2 BindNodeRepresentation happens in the scope of the wire response so we don't want to
  // do any natural type conversions.
  zx::result<std::optional<CompositeNodeAndDriver>> BindNodeRepresentation(
      fuchsia_driver_index::MatchedNodeRepresentationInfo match_info,
      const DeviceOrNode& device_or_node);

  // Exposed for testing only.
  const NodeGroupMap& node_groups() const { return node_groups_; }

 private:
  // Contains all node groups. This maps the name to a NodeGroup object.
  NodeGroupMap node_groups_;

  // The owner of NodeGroupManager. CompositeManagerBridge must outlive NodeGroupManager.
  CompositeManagerBridge* bridge_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_NODE_GROUP_NODE_GROUP_MANAGER_H_
