// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_H_

#include <fidl/fuchsia.driver.index/cpp/fidl.h>

struct DeviceV1Wrapper;

namespace dfv2 {
class Node;
}  // namespace dfv2

using DeviceOrNode = std::variant<std::weak_ptr<DeviceV1Wrapper>, std::weak_ptr<dfv2::Node>>;

struct DeviceGroupCreateInfo {
  std::string topological_path;
  size_t size;
};

// This partially abstract class represents a device group and is responsible for managing
// its state and composite node. The DeviceGroup class will manage the state of its bound
// nodes while its subclasses manage the composite node under the device group. There should
// be a subclass for DFv1 and DFv2.
class DeviceGroup {
 public:
  // TODO(fxb/108360): Take in a primary_node_index when that is available in the match info.
  explicit DeviceGroup(DeviceGroupCreateInfo create_info);

  virtual ~DeviceGroup() = default;

  // Called when DeviceGroupManager receives a MatchedDeviceGroupNode.
  // Returns ZX_ERR_ALREADY_BOUND if it's already bound. See BindNodeImpl() for return type details.
  zx::status<std::optional<DeviceOrNode>> BindNode(
      fuchsia_driver_index::wire::MatchedDeviceGroupInfo info, const DeviceOrNode& device_or_node);

  // Exposed for testing.
  const std::vector<bool>& device_group_nodes() const { return device_group_nodes_; }

 protected:
  // Subclass implementation for binding the DeviceOrNode to its composite. If the composite is not
  // yet created, the implementation is expected to create one with |info|. In DFv1, it returns
  // std::nullopt. In DFv2, if the composite is complete, it returns a pointer to the new node.
  // Otherwise, it returns std::nullopt. The lifetime of this node object is managed by
  // the parent nodes.
  virtual zx::status<std::optional<DeviceOrNode>> BindNodeImpl(
      fuchsia_driver_index::wire::MatchedDeviceGroupInfo info,
      const DeviceOrNode& device_or_node) = 0;

 private:
  std::string topological_path_;
  std::vector<bool> device_group_nodes_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_H_
