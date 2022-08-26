// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_COMPOSITE_MANAGER_BRIDGE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_COMPOSITE_MANAGER_BRIDGE_H_

#include <fidl/fuchsia.driver.index/cpp/fidl.h>

class DeviceGroup;
struct DeviceV1Wrapper;
class Node;

using DeviceOrNode = std::variant<std::weak_ptr<DeviceV1Wrapper>, std::weak_ptr<Node>>;

// Bridge class for the composite device handling in DFv1 and DFv2.
// Implemented by the Coordinator in DFv1 and DriverRunner in DFv2.
class CompositeManagerBridge {
 public:
  virtual ~CompositeManagerBridge() = default;

  virtual zx::status<std::unique_ptr<DeviceGroup>> CreateDeviceGroup(
      fuchsia_driver_framework::wire::DeviceGroup group,
      fuchsia_driver_index::MatchedCompositeInfo driver) = 0;

  // Match and bind all unbound nodes. Called by the DeviceGroupManager
  // after a device group is matched with a composite driver.
  virtual void BindNodesForDeviceGroups() = 0;

  virtual zx::status<fuchsia_driver_index::MatchedCompositeInfo> AddDeviceGroupToDriverIndex(
      fuchsia_driver_framework::wire::DeviceGroup group) = 0;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_COMPOSITE_MANAGER_BRIDGE_H_
