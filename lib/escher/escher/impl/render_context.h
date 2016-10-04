// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include <vector>

#include "escher/impl/swapchain_manager.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"
#include "escher/vk/vulkan_context.h"
#include "escher/vk/vulkan_swapchain.h"
#include "ftl/macros.h"

namespace escher {
namespace impl {

class TempFrameRenderer;

// Type returned by AllocateCommandBuffers().  Same as in vulkan.hpp.
typedef vk::ResultValueType<std::vector<vk::CommandBuffer>>::type
    AllocateCommandBuffersResult;

// Manages resources required to render frames, ensuring that they are not
// destroyed before all frames that use them are finished.
class RenderContext {
 public:
  RenderContext(const VulkanContext& context);
  ~RenderContext();

  vk::Result Initialize(const VulkanSwapchain& swapchain);

  // Render a frame.
  vk::Result Render(const Stage& stage, const Model& model);

  // Called by EscherImpl.
  void SetSwapchain(const VulkanSwapchain& swapchain);

  // Supports allocation, tracking, and cleanup of per-frame resources.
  class Frame {
   public:
    Frame(Frame&& other);

    // Allocate a vector of CommandBuffers, which will all be cleaned up once
    // the frame is completed (the caller is not responsible for managing their
    // lifecycle).  It is illegal to use these buffers outside of the invocation
    // of Frame::Render() during which they were allocated.
    AllocateCommandBuffersResult AllocateCommandBuffers(
        uint32_t count,
        vk::CommandBufferLevel level);

    RenderContext* GetRenderContext() { return render_context_; }

    uint64_t frame_number() { return frame_number_; }

   private:
    Frame(RenderContext* context, uint64_t frame_number);

    // Possible errors: Timeout, NotReady, OutOfHostMemory, OutOfDeviceMemory.
    vk::Result Render(const Stage& stage,
                      const Model& model,
                      vk::Framebuffer framebuffer);

    RenderContext* render_context_;
    const VulkanContext& vulkan_context_;

    vk::Fence fence_;
    std::vector<vk::CommandBuffer> buffers_;
    uint64_t frame_number_;

    friend class RenderContext;
    FTL_DISALLOW_COPY_AND_ASSIGN(Frame);
  };

 private:
  vk::Result UpdateDeviceAndSwapchain();
  void CleanupFinishedFrames();
  void CleanupFrame(Frame* frame);
  vk::Result CreateRenderPass();
  vk::Result CreateCommandPool();
  void DestroyCommandPool();

  VulkanContext context_;

  vk::Format depth_format_ = vk::Format::eUndefined;
  vk::RenderPass render_pass_;
  vk::CommandPool command_pool_;
  std::unique_ptr<ImageCache> image_cache_;
  std::unique_ptr<SwapchainManager> swapchain_manager_;
  std::unique_ptr<TempFrameRenderer> temp_frame_renderer_;

  std::queue<Frame> pending_frames_;
  uint64_t frame_number_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(RenderContext);
};

}  // namespace impl
}  // namespace escher
