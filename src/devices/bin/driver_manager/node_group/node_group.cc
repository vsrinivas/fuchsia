// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/node_group/node_group.h"

#include "src/devices/lib/log/log.h"

NodeGroup::NodeGroup(NodeGroupCreateInfo create_info) : name_(create_info.name) {
  node_representations_ = std::vector<bool>(create_info.size, false);
}

zx::result<std::optional<DeviceOrNode>> NodeGroup::BindNode(
    fuchsia_driver_index::wire::MatchedNodeGroupInfo info, const DeviceOrNode& device_or_node) {
  ZX_ASSERT(info.has_node_index());
  auto node_index = info.node_index();
  if (node_index >= node_representations_.size()) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  if (node_representations_[node_index]) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }

  auto result = BindNodeImpl(info, device_or_node);
  if (result.is_ok()) {
    node_representations_[node_index] = true;
  }

  return result;
}
