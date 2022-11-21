// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_NODE_GROUP_NODE_GROUP_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_NODE_GROUP_NODE_GROUP_H_

#include <fidl/fuchsia.driver.index/cpp/fidl.h>

struct DeviceV1Wrapper;

namespace dfv2 {
class Node;
}  // namespace dfv2

using DeviceOrNode = std::variant<std::weak_ptr<DeviceV1Wrapper>, std::weak_ptr<dfv2::Node>>;

struct NodeGroupCreateInfo {
  std::string name;
  size_t size;
};

// This partially abstract class represents a node group and is responsible for managing
// its state and composite node. The NodeGroup class will manage the state of its bound
// nodes while its subclasses manage the composite node under the node group. There should
// be a subclass for DFv1 and DFv2.
class NodeGroup {
 public:
  // TODO(fxb/108360): Take in a primary_node_index when that is available in the match info.
  explicit NodeGroup(NodeGroupCreateInfo create_info);

  virtual ~NodeGroup() = default;

  // Called when NodeGroupManager receives a MatchedNodeRepresentation.
  // Returns ZX_ERR_ALREADY_BOUND if it's already bound. See BindNodeImpl() for return type details.
  zx::result<std::optional<DeviceOrNode>> BindNode(
      fuchsia_driver_index::wire::MatchedNodeGroupInfo info, const DeviceOrNode& device_or_node);

  // Exposed for testing.
  const std::vector<bool>& node_representations() const { return node_representations_; }

 protected:
  // Subclass implementation for binding the DeviceOrNode to its composite. If the composite is not
  // yet created, the implementation is expected to create one with |info|. In DFv1, it returns
  // std::nullopt. In DFv2, if the composite is complete, it returns a pointer to the new node.
  // Otherwise, it returns std::nullopt. The lifetime of this node object is managed by
  // the parent nodes.
  virtual zx::result<std::optional<DeviceOrNode>> BindNodeImpl(
      fuchsia_driver_index::wire::MatchedNodeGroupInfo info,
      const DeviceOrNode& device_or_node) = 0;

 private:
  std::string name_;
  std::vector<bool> node_representations_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_NODE_GROUP_NODE_GROUP_H_
