// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/examples/vkproto/common/command_pool.h"

#include "src/graphics/examples/vkproto/common/utils.h"

namespace vkp {

CommandPool::CommandPool(std::shared_ptr<vk::Device> device, uint32_t queue_family_index)
    : initialized_(false), device_(device), queue_family_index_(queue_family_index) {}

bool CommandPool::Init() {
  RTN_IF_MSG(false, initialized_, "CommandPool is already initialized.\n");
  RTN_IF_MSG(false, !device_, "Device must be initialized.\n");

  vk::CommandPoolCreateInfo pool_info;
  pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
  pool_info.queueFamilyIndex = queue_family_index_;

  auto [r_command_pool, command_pool] = device_->createCommandPoolUnique(pool_info);
  RTN_IF_VKH_ERR(false, r_command_pool, "Failed to create command pool.\n");
  command_pool_ = std::move(command_pool);

  initialized_ = true;
  return true;
}

}  // namespace vkp
