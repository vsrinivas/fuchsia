// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DEVICE_GROUP_V2_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DEVICE_GROUP_V2_H_

#include "src/devices/bin/driver_manager/device_group/device_group.h"
#include "src/devices/bin/driver_manager/v2/parent_set_collector.h"

namespace dfv2 {

class DeviceGroupV2 : public DeviceGroup {
 public:
  // Must only be called by Create() to ensure the objects are verified.
  DeviceGroupV2(DeviceGroupCreateInfo create_info, std::string_view composite_name,
                fuchsia_driver_index::MatchedDriverInfo driver_info, async_dispatcher_t* dispatcher,
                NodeManager* node_manager);

  ~DeviceGroupV2() override = default;

  static zx::status<std::unique_ptr<DeviceGroupV2>> Create(
      DeviceGroupCreateInfo create_info, fuchsia_driver_index::MatchedCompositeInfo driver,
      async_dispatcher_t* dispatcher, NodeManager* node_manager);

 protected:
  zx::status<std::optional<DeviceOrNode>> BindNodeImpl(uint32_t node_index,
                                                       const DeviceOrNode& device_or_node) override;

 private:
  // Gets a vector of all the node names. Only call this method if all nodes have been bound.
  std::vector<std::string> GetNodeNames() const;

  fuchsia_driver_index::MatchedDriverInfo driver_info_;
  ParentSetCollector parent_set_collector_;
  async_dispatcher_t* const dispatcher_;
  NodeManager* node_manager_;
};

}  // namespace dfv2

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DEVICE_GROUP_V2_H_
