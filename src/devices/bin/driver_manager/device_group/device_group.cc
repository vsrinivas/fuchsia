// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device_group/device_group.h"

#include "src/devices/lib/log/log.h"

DeviceGroup::DeviceGroup(DeviceGroupCreateInfo create_info)
    : topological_path_(create_info.topological_path) {
  device_group_nodes_ = std::vector<bool>(create_info.size, false);
}

zx::result<std::optional<DeviceOrNode>> DeviceGroup::BindNode(
    fuchsia_driver_index::wire::MatchedDeviceGroupInfo info, const DeviceOrNode& device_or_node) {
  ZX_ASSERT(info.has_node_index());
  auto node_index = info.node_index();
  if (node_index >= device_group_nodes_.size()) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  if (device_group_nodes_[node_index]) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }

  auto result = BindNodeImpl(info, device_or_node);
  if (result.is_ok()) {
    device_group_nodes_[node_index] = true;
  }

  return result;
}
