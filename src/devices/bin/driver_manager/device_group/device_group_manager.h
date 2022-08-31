// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_MANAGER_H_

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.driver.index/cpp/fidl.h>
#include <lib/zx/status.h>

#include <unordered_map>

#include "src/devices/bin/driver_manager/device_group/device_group.h"

// This class is responsible for managing device groups. It keeps track of the device
// groups and its matching composite driver and nodes. DeviceGroupManager is owned by a
// CompositeManagerBridge and must be outlived by it.
class DeviceGroupManager {
 public:
  using DeviceGroupMap = std::unordered_map<std::string, std::unique_ptr<DeviceGroup>>;

  explicit DeviceGroupManager(CompositeManagerBridge* bridge);

  // Adds a device group to the driver index. If it's successfully added, then the
  // DeviceGroupManagere stores the device group in a map.
  zx::status<> AddDeviceGroup(fuchsia_driver_framework::wire::DeviceGroup group);

  // Receives this call from CompositeManagerBridge when a matched composite driver is found
  // for a device group. This function creates a DeviceGroup object and adds it into
  // |device_groups_|. After that, it sends a call to CompositeManagerBridge to bind all
  // unbound devices.
  zx::status<> BindAndCreateDeviceGroup(size_t size, std::string_view topological_path,
                                        fuchsia_driver_index::MatchedCompositeInfo driver);

  // Receives this call from CompositeManagerBridge when a device/node is matched to a device group
  // node. DeviceGroupManager will go through the list of device groups until it finds one with
  // the node unbound.
  zx::status<> BindDeviceGroupNode(fuchsia_driver_index::MatchedDeviceGroupNodeInfo match_info,
                                   DeviceOrNode node);

  // Exposed for testing only.
  const DeviceGroupMap& device_groups() const { return device_groups_; }

 private:
  // Contains all device groups. This maps the topological path to a DeviceGroup object.
  // If matching composite driver has not been found for the device group, then the
  // entry is set to null.
  DeviceGroupMap device_groups_;

  // The owner of DeviceGroupManager. CompositeManagerBridge must outlive DeviceGroupManager.
  CompositeManagerBridge* bridge_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_DEVICE_GROUP_DEVICE_GROUP_MANAGER_H_
