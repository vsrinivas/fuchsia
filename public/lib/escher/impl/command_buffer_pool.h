// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>

#include "escher/impl/command_buffer.h"
#include "escher/impl/command_buffer_sequencer.h"
#include "escher/vk/vulkan_context.h"
#include "lib/fxl/macros.h"

namespace escher {
namespace impl {

class CommandBuffer;

// Manages the lifecycle of CommandBuffers.
//
// Not thread-safe.
class CommandBufferPool : CommandBufferSequencerController {
 public:
  // The CommandBufferPool does not take ownership of the device and queue.
  CommandBufferPool(vk::Device device,
                    vk::Queue queue,
                    uint32_t queue_family_index,
                    CommandBufferSequencer* sequencer,
                    bool supports_graphics_and_compute);

  // If there are still any pending buffers, this will block until they are
  // finished.
  ~CommandBufferPool();

  // Get a ready-to-use CommandBuffer; a new one will be allocated if necessary.
  // If a callback is provided, it will be run at some time after the buffer
  // has finished running on the GPU.
  CommandBuffer* GetCommandBuffer();

  // Do periodic housekeeping.
  void Cleanup();

  vk::Device device() const { return device_; }
  vk::Queue queue() const { return queue_; }

 private:
  const vk::Device device_;
  const vk::Queue queue_;
  // Rule out pipeline stages that are not supported on our queue.
  vk::PipelineStageFlags pipeline_stage_mask_;

  CommandBufferSequencer* const sequencer_;

  // TODO: access to |command_pool_| needs to be externally synchronized.  This
  // includes implicit uses such as various vkCmd* calls (in other words, two
  // separate CommandBuffers obtained from this pool cannot be recorded into
  // concurrently).  See Vulkan Spec Sec 2.5 under "Implicit Externally
  // Synchronized Parameters".
  vk::CommandPool pool_;
  std::queue<std::unique_ptr<CommandBuffer>> free_buffers_;
  std::queue<std::unique_ptr<CommandBuffer>> pending_buffers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CommandBufferPool);
};

}  // namespace impl
}  // namespace escher
