// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/device_group_v2.h"

#include "src/devices/lib/log/log.h"

namespace dfv2 {

zx::status<std::unique_ptr<DeviceGroupV2>> DeviceGroupV2::Create(
    DeviceGroupCreateInfo create_info, fuchsia_driver_index::MatchedCompositeInfo driver,
    async_dispatcher_t* dispatcher, NodeManager* node_manager) {
  auto info = driver.driver_info();
  auto name = driver.composite_name();
  if (!info.has_value() || !name.has_value()) {
    LOGF(ERROR, "Composite driver is missing driver info or composite_name.");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  auto driver_info = info.value();
  auto composite_name = name.value();
  auto url = driver_info.url();
  if (!url.has_value() || url.value().empty()) {
    LOGF(ERROR, "Composite driver url is empty");
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  return zx::ok(std::make_unique<DeviceGroupV2>(std::move(create_info), composite_name, driver_info,
                                                dispatcher, node_manager));
}

DeviceGroupV2::DeviceGroupV2(DeviceGroupCreateInfo create_info, std::string_view composite_name,
                             fuchsia_driver_index::MatchedDriverInfo driver_info,
                             async_dispatcher_t* dispatcher, NodeManager* node_manager)
    : DeviceGroup(create_info, composite_name),
      driver_info_(std::move(driver_info)),
      parent_set_collector_(create_info.size),
      dispatcher_(dispatcher),
      node_manager_(node_manager) {}

zx::status<std::optional<DeviceOrNode>> DeviceGroupV2::BindNodeImpl(
    uint32_t node_index, const DeviceOrNode& device_or_node) {
  auto node_ptr = std::get_if<std::weak_ptr<dfv2::Node>>(&device_or_node);
  ZX_ASSERT(node_ptr);
  if (parent_set_collector_.ContainsNode(node_index)) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }

  parent_set_collector_.AddNode(node_index, *node_ptr);

  // Check if we have all the nodes for the device group.
  auto completed_parents = parent_set_collector_.GetIfComplete();
  if (!completed_parents.has_value()) {
    // Parent set is not complete yet.
    return zx::ok(std::nullopt);
  }

  // Create a composite node for the device group with our complete parent set.
  auto composite = Node::CreateCompositeNode(composite_name(), std::move(*completed_parents),
                                             node_names(), {}, node_manager_, dispatcher_);
  if (composite.is_error()) {
    // If we are returning an error we should clear out what we have.
    parent_set_collector_.RemoveNode(node_index);
    return composite.take_error();
  }

  // We can return a pointer, as the composite node is owned by its parents.
  return zx::ok(composite.value()->weak_from_this());
}

}  // namespace dfv2
