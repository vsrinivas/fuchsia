// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device_group/device_group.h"

#include "src/devices/lib/log/log.h"

namespace fdi = fuchsia_driver_index;
namespace fdf = fuchsia_driver_framework;

DeviceGroup::DeviceGroup(size_t size) { device_group_nodes_ = std::vector<bool>(size, false); }

zx::status<> DeviceGroup::BindNode(uint32_t node_index, DeviceOrNode node) {
  if (node_index >= device_group_nodes_.size()) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  if (device_group_nodes_[node_index]) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }

  auto result = BindNodeToComposite(node_index, std::move(node));
  if (result.is_ok()) {
    device_group_nodes_[node_index] = true;
  }

  return result;
}
