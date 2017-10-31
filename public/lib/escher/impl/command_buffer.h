// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <vector>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/renderer/semaphore_wait.h"
#include "lib/escher/vk/vulkan_context.h"

#include "lib/fxl/macros.h"

namespace escher {
typedef std::function<void()> CommandBufferFinishedCallback;

namespace impl {

// CommandBuffer is a wrapper around vk::CommandBuffer.  Vulkan forbids the
// client application from destroying any resources while they are used by
// any "pending command buffers" (i.e. those that have not finished executing
// on the GPU).
//
// CommandBuffers are obtained from a CommandBufferPool, and is automatically
// returned to it when all GPU-work is finished.
//
// Not thread-safe.
class CommandBuffer {
 public:
  ~CommandBuffer();

  vk::CommandBuffer get() const { return command_buffer_; }

  // Return true if successful.  The callback will be invoked after all commands
  // have finished executing on the GPU (there is no guarantee about how long
  // afterward: this depends on when the CommandBufferPool that owns this buffer
  // calls Retire()).
  bool Submit(vk::Queue queue, CommandBufferFinishedCallback callback);

  // During Submit(), these semaphores will be added to the vk::SubmitInfo.
  // No-op if semaphore is null.
  void AddWaitSemaphore(SemaphorePtr semaphore, vk::PipelineStageFlags stage);

  // For convenience, calls TakeWaitSemaphore() on the resource, and passes the
  // result to AddWaitSemaphore().
  template <typename ResourcePtrT>
  void TakeWaitSemaphore(const ResourcePtrT& resource,
                         vk::PipelineStageFlags stage) {
    AddWaitSemaphore(resource->TakeWaitSemaphore(), stage);
  }

  // During Submit(), these semaphores will be added to the vk::SubmitInfo.
  // No-op if semaphore is null.
  void AddSignalSemaphore(SemaphorePtr semaphore);

  // These resources will be retained until the command-buffer is finished
  // running on the GPU.
  void KeepAlive(Resource* resource);
  template <typename ResourceT>
  void KeepAlive(const fxl::RefPtr<ResourceT>& ptr) {
    KeepAlive(ptr.get());
  }

  // Bind index/vertex buffers and write draw command.
  // Retain mesh in used_resources.
  void DrawMesh(const MeshPtr& mesh);

  // Copy pixels from one image to another.  No image barriers or other
  // synchronization is used.  Retain both images in used_resources.
  void CopyImage(const ImagePtr& src_image,
                 const ImagePtr& dst_image,
                 vk::ImageLayout src_layout,
                 vk::ImageLayout dst_layout,
                 vk::ImageCopy* region);

  // Copy memory from one buffer to another.
  void CopyBuffer(const BufferPtr& src,
                  const BufferPtr& dst,
                  vk::BufferCopy region);

  // Transition the image between the two layouts; see section 11.4 of the
  // Vulkan spec.  Retain image in used_resources.
  void TransitionImageLayout(const ImagePtr& image,
                             vk::ImageLayout old_layout,
                             vk::ImageLayout new_layout);

  // Convenient way to begin a render-pass that renders to the whole framebuffer
  // (i.e. width/height of viewport and scissors are obtained from framebuffer).
  void BeginRenderPass(const RenderPassPtr& render_pass,
                       const FramebufferPtr& framebuffer,
                       const std::vector<vk::ClearValue>& clear_values);
  void BeginRenderPass(vk::RenderPass render_pass,
                       const FramebufferPtr& framebuffer,
                       const std::vector<vk::ClearValue>& clear_values);
  void BeginRenderPass(vk::RenderPass render_pass,
                       const FramebufferPtr& framebuffer,
                       const vk::ClearValue* clear_values,
                       size_t clear_value_count);

  // Simple wrapper around endRenderPass().
  void EndRenderPass();

  // Block until the command-buffer is no longer pending, or the specified
  // number of nanoseconds has elapsed.  Return vk::Result::eSuccess in the
  // former case, and vk::Result::eTimeout in the latter.
  vk::Result Wait(uint64_t timeout_nanoseconds);

  // Each CommandBuffer that is obtained from a CommandBufferPool is given a
  // monotonically-increasing sequence number.  This number is globally unique
  // (per Escher instance), even across multiple CommandBufferPools.
  uint64_t sequence_number() const { return sequence_number_; }

 private:
  friend class CommandBufferPool;

  // Called by CommandBufferPool, which is responsible for eventually destroying
  // the Vulkan command buffer and fence.  Submit() and Retire() use the fence
  // to determine when the command buffer has finished executing on the GPU.
  CommandBuffer(vk::Device device,
                vk::CommandBuffer command_buffer,
                vk::Fence fence,
                vk::PipelineStageFlags pipeline_stage_mask);
  vk::Fence fence() const { return fence_; }

  // Called by CommandBufferPool when this buffer is obtained from it.
  void Begin(uint64_t sequence_number);

  // Called by CommandBufferPool, to attempt to reset the buffer for reuse.
  // Return false and do nothing if the buffer's submission fence is not ready.
  bool Retire();

  const vk::Device device_;
  const vk::CommandBuffer command_buffer_;
  const vk::Fence fence_;
  const vk::PipelineStageFlags pipeline_stage_mask_;

  std::vector<ResourcePtr> used_resources_;

  std::vector<SemaphorePtr> wait_semaphores_;
  std::vector<vk::PipelineStageFlags> wait_semaphore_stages_;
  std::vector<vk::Semaphore> wait_semaphores_for_submit_;

  std::vector<SemaphorePtr> signal_semaphores_;
  std::vector<vk::Semaphore> signal_semaphores_for_submit_;

  bool is_active_ = false;
  bool is_submitted_ = false;

  uint64_t sequence_number_ = 0;

  CommandBufferFinishedCallback callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CommandBuffer);
};

}  // namespace impl
}  // namespace escher
