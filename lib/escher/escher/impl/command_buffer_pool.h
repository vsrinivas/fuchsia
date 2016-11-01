// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>

#include "escher/impl/command_buffer.h"
#include "escher/vk/vulkan_context.h"
#include "ftl/macros.h"

namespace escher {
namespace impl {

class CommandBuffer;

// Manages the lifecycle of CommandBuffers.
//
// Not thread-safe.
class CommandBufferPool {
 public:
  CommandBufferPool(const VulkanContext& context);

  // If there are still any pending buffers, this will block until they are
  // finished.
  ~CommandBufferPool();

  // Get a ready-to-use CommandBuffer; a new one will be allocated if necessary.
  // If a callback is provided, it will be run at some time after the buffer
  // has finished running on the GPU.
  CommandBuffer* GetCommandBuffer(CommandBufferFinishedCallback callback);

  // Do periodic housekeeping.
  void Cleanup();

 private:
  VulkanContext context_;
  vk::CommandPool pool_;
  std::queue<std::unique_ptr<CommandBuffer>> free_buffers_;
  std::queue<std::unique_ptr<CommandBuffer>> pending_buffers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CommandBufferPool);
};

}  // namespace impl
}  // namespace escher
