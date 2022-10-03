// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_MANAGER_H_

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.driver.index/cpp/fidl.h>
#include <lib/zx/status.h>

#include <unordered_map>

#include "src/devices/bin/driver_manager/device_group/composite_manager_bridge.h"

struct CompositeNodeAndDriver {
  fuchsia_driver_index::wire::MatchedDriverInfo driver;
  DeviceOrNode node;
};

// This class is responsible for managing device groups. It keeps track of the device
// groups and its matching composite driver and nodes. DeviceGroupManager is owned by a
// CompositeManagerBridge and must be outlived by it.
class DeviceGroupManager : public fidl::WireServer<fuchsia_driver_framework::DeviceGroupManager> {
 public:
  using DeviceGroupMap = std::unordered_map<std::string, std::unique_ptr<DeviceGroup>>;

  explicit DeviceGroupManager(CompositeManagerBridge* bridge);

  // fidl::WireServer<fuchsia_driver_framework::DeviceGroupManager>
  void CreateDeviceGroup(CreateDeviceGroupRequestView request,
                         CreateDeviceGroupCompleter::Sync& completer) override;

  // Adds a device group to the driver index. If it's successfully added, then the
  // DeviceGroupManager stores the device group in a map. After that, it sends a call to
  // CompositeManagerBridge to bind all unbound devices.
  fitx::result<fuchsia_driver_framework::DeviceGroupError> AddDeviceGroup(
      fuchsia_driver_framework::wire::DeviceGroup group);

  // Binds the device to one of the device group nodes that it was matched to.
  // DeviceGroupManager will go through the list of device groups until it finds one with
  // the node unbound.
  // DFv1:
  // This will internally use device_group_v1, which itself uses
  // CompositeDevice's BindFragment to do all the work needed to track the composite fragments
  // and to start the driver.
  // A std::nullopt is always returned.
  // DFv2:
  // This will use device_group_v2, which internally tracks the nodes in a ParentSetCollector,
  // when the parent set is completed, a child node is created that is parented by all the nodes
  // from the parent set.
  // A std::nullopt is returned if the chosen device group is not yet complete, otherwise a
  // shared pointer to the newly created child node is returned along with the driver of the
  // chosen match. DriverRunner is responsible for starting the driver on the node.
  zx::status<std::optional<CompositeNodeAndDriver>> BindDeviceGroupNode(
      fuchsia_driver_index::wire::MatchedDeviceGroupNodeInfo match_info,
      const DeviceOrNode& device_or_node);

  // Reason for both versions of this method is that in DFv1 the match info is stored
  // via natural types because BindDeviceGroupNode is outside of the fidl wire response's scope.
  // In DFv2 BindDeviceGroupNode happens in the scope of the wire response so we don't want to
  // do any natural type conversions.
  zx::status<std::optional<CompositeNodeAndDriver>> BindDeviceGroupNode(
      fuchsia_driver_index::MatchedDeviceGroupNodeInfo match_info,
      const DeviceOrNode& device_or_node);

  // Exposed for testing only.
  const DeviceGroupMap& device_groups() const { return device_groups_; }

 private:
  // This function creates a DeviceGroup object and adds it into |device_groups_|.
  // It is called by |AddDeviceGroup| and |BindDeviceGroupNode|.
  zx::status<> CreateDeviceGroup(DeviceGroupCreateInfo create_info,
                                 fuchsia_driver_index::MatchedCompositeInfo driver);

  // Contains all device groups. This maps the topological path to a DeviceGroup object.
  // If matching composite driver has not been found for the device group, then the
  // entry is set to null.
  DeviceGroupMap device_groups_;

  // The owner of DeviceGroupManager. CompositeManagerBridge must outlive DeviceGroupManager.
  CompositeManagerBridge* bridge_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_MANAGER_H_
