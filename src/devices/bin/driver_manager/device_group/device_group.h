// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_H_

#include "src/devices/bin/driver_manager/device_group/composite_manager_bridge.h"

// This partially abstract class represents a device group and is responsible for managing
// its state and composite node. The DeviceGroup class will manage the state of its bound
// nodes while its subclasses manage the composite node under the device group. There should
// be a subclass for DFv1 and DFv2.
class DeviceGroup {
 public:
  explicit DeviceGroup(size_t size);

  virtual ~DeviceGroup() = default;

  // Called when DeviceGroupManager receives a MatchedDeviceGroupNode.
  // Returns ZX_ERR_ALREADY_BOUND if it's already bound.
  zx::status<> BindNode(uint32_t node_index, DeviceOrNode node);

  // Exposed for testing.
  const std::vector<bool>& device_group_nodes() const { return device_group_nodes_; }

 protected:
  // Binds the given node to the composite. If all nodes are bound, create the composite.
  // Subclasses are responsible for managing the composite.
  virtual zx::status<> BindNodeToComposite(uint32_t node_index, DeviceOrNode node) = 0;

 private:
  std::vector<bool> device_group_nodes_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_H_
