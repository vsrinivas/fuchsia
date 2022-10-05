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
  DeviceGroupV2(DeviceGroupCreateInfo create_info, async_dispatcher_t* dispatcher,
                NodeManager* node_manager);

  ~DeviceGroupV2() override = default;

 protected:
  zx::status<std::optional<DeviceOrNode>> BindNodeImpl(
      fuchsia_driver_index::wire::MatchedDeviceGroupInfo info,
      const DeviceOrNode& device_or_node) override;

 private:
  std::optional<ParentSetCollector> parent_set_collector_;
  async_dispatcher_t* const dispatcher_;
  NodeManager* node_manager_;
};

}  // namespace dfv2

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V2_DEVICE_GROUP_V2_H_
