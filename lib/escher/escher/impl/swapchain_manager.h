// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <vulkan/vulkan.hpp>

#include "escher/impl/image_cache.h"
#include "escher/vk/vulkan_context.h"
#include "escher/vk/vulkan_swapchain.h"

namespace escher {
namespace impl {

// Synchronize rendering and presentation of swapchain images.
class SwapchainManager {
 public:
  SwapchainManager(const VulkanContext& context,
                   vk::RenderPass render_pass,
                   ImageCache* image_cache,
                   vk::Format depth_format);
  ~SwapchainManager();

  vk::Result Init();

  // Per-frame information which is obtained from BeginFrame(), and will remain
  // valid until the next time EndFrame() is called.
  struct FrameInfo {
    vk::Framebuffer framebuffer;
    vk::Semaphore image_available_semaphore;
    vk::Semaphore render_finished_semaphore;
  };
  // Result type for returning a FrameInfo.
  typedef vk::ResultValueType<FrameInfo>::type FrameInfoResult;

  // Return a FrameInfo* and eSuccess, or nullptr and an error.  Possible errors
  // are Timeout and NotReady.
  FrameInfoResult BeginFrame();
  void EndFrame();

  // Regenerate framebuffers to match new swapchain configuration.
  void SetSwapchain(const VulkanSwapchain& swapchain);

 private:
  VulkanContext context_;
  vk::RenderPass render_pass_;
  ImageCache* image_cache_;
  vk::Semaphore image_available_semaphore_;
  vk::Semaphore render_finished_semaphore_;
  vk::Format depth_format_;
  ftl::RefPtr<Image> depth_image_;
  vk::ImageView depth_image_view_;
  uint32_t width_ = UINT32_MAX;
  uint32_t height_ = UINT32_MAX;
  std::vector<vk::Framebuffer> framebuffers_;
  bool in_frame_ = false;
  VulkanSwapchain swapchain_;
  uint32_t swapchain_index_ = UINT32_MAX;  // invalid

  FrameInfo frame_info_;
};

}  // namespace impl
}  // namespace escher
