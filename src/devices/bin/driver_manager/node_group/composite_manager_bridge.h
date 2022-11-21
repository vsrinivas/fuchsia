// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_NODE_GROUP_COMPOSITE_MANAGER_BRIDGE_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_NODE_GROUP_COMPOSITE_MANAGER_BRIDGE_H_

#include "src/devices/bin/driver_manager/node_group/node_group.h"

using AddToIndexCallback =
    fit::callback<void(zx::result<fuchsia_driver_index::DriverIndexAddNodeGroupResponse>)>;

// Bridge class for the composite device handling in DFv1 and DFv2.
// Implemented by the Coordinator in DFv1 and DriverRunner in DFv2.
class CompositeManagerBridge {
 public:
  virtual ~CompositeManagerBridge() = default;

  // Match and bind all unbound nodes. Called by the NodeGroupManager
  // after a node group is matched with a composite driver.
  virtual void BindNodesForNodeGroups() = 0;

  virtual void AddNodeGroupToDriverIndex(fuchsia_driver_framework::wire::NodeGroup group,
                                         AddToIndexCallback callback) = 0;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_NODE_GROUP_COMPOSITE_MANAGER_BRIDGE_H_
