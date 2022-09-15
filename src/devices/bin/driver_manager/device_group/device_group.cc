// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device_group/device_group.h"

#include "src/devices/lib/log/log.h"

DeviceGroup::DeviceGroup(DeviceGroupCreateInfo create_info, std::string_view composite_name)
    : topological_path_(create_info.topological_path),
      composite_name_(composite_name),
      node_names_(std::move(create_info.node_names)) {
  device_group_nodes_ = std::vector<bool>(create_info.size, false);
}

zx::status<std::optional<DeviceOrNode>> DeviceGroup::BindNode(uint32_t node_index,
                                                              const DeviceOrNode& device_or_node) {
  if (node_index >= device_group_nodes_.size()) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  if (device_group_nodes_[node_index]) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }

  auto result = BindNodeImpl(node_index, device_or_node);
  if (result.is_ok()) {
    device_group_nodes_[node_index] = true;
  }

  return result;
}
