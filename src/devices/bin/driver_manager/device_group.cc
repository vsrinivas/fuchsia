// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/device_group.h"

#include "src/devices/bin/driver_manager/v1/init_task.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_resume_manager.h"
#include "src/devices/bin/driver_manager/v1/suspend_task.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"
#include "src/devices/lib/log/log.h"

namespace fdi = fuchsia_driver_index;
namespace fdf = fuchsia_driver_framework;

DeviceGroup::DeviceGroup(fuchsia_driver_framework::wire::DeviceGroup group) {
  ZX_ASSERT(group.has_nodes() && !group.nodes().empty());

  device_group_nodes_ = fbl::Array<DeviceGroupNode>(new DeviceGroupNode[group.nodes().count()],
                                                    group.nodes().count());

  for (size_t i = 0; i < device_group_nodes_.size(); i++) {
    device_group_nodes_[i] = DeviceGroupNode{
        .name = std::string(group.nodes().at(i).name.get()),
        .is_bound = false,
    };
  }
}

zx::status<> DeviceGroup::BindNode(uint32_t node_index, DeviceOrNode node) {
  if (node_index >= device_group_nodes_.size()) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  if (device_group_nodes_[node_index].is_bound) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }

  auto result = BindNodeToComposite(node_index, std::move(node));
  if (result.is_ok()) {
    device_group_nodes_[node_index].is_bound = true;
  }

  return result;
}
