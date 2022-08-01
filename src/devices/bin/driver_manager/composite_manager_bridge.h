// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_COMPOSITE_MANAGER_BRIDGE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_COMPOSITE_MANAGER_BRIDGE_H_

#include "src/devices/bin/driver_manager/device.h"
#include "src/devices/bin/driver_manager/v1/init_task.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_task.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"

class DeviceGroup;
class Node;

using DeviceOrNode = std::variant<const fbl::RefPtr<Device>, std::weak_ptr<Node>>;

// Bridge class for the composite device handling in DFv1 and DFv2.
// Implemented by the Coordinator in DFv1 and DriverRunner in DFv2.
class CompositeManagerBridge {
 public:
  virtual ~CompositeManagerBridge() = default;

  virtual zx::status<std::unique_ptr<DeviceGroup>> CreateDeviceGroup(
      fuchsia_driver_framework::wire::DeviceGroup group,
      fuchsia_driver_index::wire::MatchedCompositeInfo driver) = 0;

  // Match and bind all unbound nodes. Called by the DeviceGroupManager
  // after a device group is matched with a composite driver.
  virtual void MatchAndBindAllNodes() = 0;

  virtual zx::status<fuchsia_driver_index::wire::MatchedCompositeInfo> AddDeviceGroupToDriverIndex(
      fuchsia_driver_framework::wire::DeviceGroup group) = 0;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_COMPOSITE_MANAGER_BRIDGE_H_
