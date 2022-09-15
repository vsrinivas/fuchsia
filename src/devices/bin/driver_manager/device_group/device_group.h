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
  std::vector<std::string> node_names;
};

// This partially abstract class represents a device group and is responsible for managing
// its state and composite node. The DeviceGroup class will manage the state of its bound
// nodes while its subclasses manage the composite node under the device group. There should
// be a subclass for DFv1 and DFv2.
class DeviceGroup {
 public:
  // TODO(fxb/108360): Take in a primary_node_index when that is available in the match info.
  DeviceGroup(DeviceGroupCreateInfo create_info, std::string_view composite_name);

  virtual ~DeviceGroup() = default;

  // Called when DeviceGroupManager receives a MatchedDeviceGroupNode.
  // Returns ZX_ERR_ALREADY_BOUND if it's already bound.
  // See the |BindNodeImpl| method for return type details.
  zx::status<std::optional<DeviceOrNode>> BindNode(uint32_t node_index,
                                                   const DeviceOrNode& device_or_node);

  // Exposed for testing.
  const std::vector<bool>& device_group_nodes() const { return device_group_nodes_; }

 protected:
  // DF version specific implementation for binding a DeviceOrNode to the device group node.
  // DFv1:
  // Binds the given device to the composite. If all nodes are bound, create the composite.
  // Subclasses are responsible for managing the composite. Internally this uses |CompositeDevice|.
  // It will always return a std::nullopt.
  // DFv2:
  // Adds the given node to the device group set. If the device group is completed,
  // a child node is created under the device group nodes as parents. A pointer to the
  // new node is returned. The lifetime of this node object is managed by the parent nodes.
  virtual zx::status<std::optional<DeviceOrNode>> BindNodeImpl(
      uint32_t node_index, const DeviceOrNode& device_or_node) = 0;

  std::string_view composite_name() const { return composite_name_; }

  const std::vector<std::string>& node_names() { return node_names_; }

 private:
  std::string topological_path_;
  std::string composite_name_;
  std::vector<bool> device_group_nodes_;
  std::vector<std::string> node_names_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_H_
