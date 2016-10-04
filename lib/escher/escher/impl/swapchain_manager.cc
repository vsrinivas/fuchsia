// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/swapchain_manager.h"
#include "escher/impl/render_context.h"
#include "escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

SwapchainManager::SwapchainManager(const VulkanContext& context,
                                   vk::RenderPass render_pass,
                                   ImageCache* image_cache,
                                   vk::Format depth_format)
    : context_(context),
      render_pass_(render_pass),
      image_cache_(image_cache),
      depth_format_(depth_format) {}

SwapchainManager::~SwapchainManager() {
  for (auto& fb : framebuffers_)
    context_.device.destroyFramebuffer(fb);
  if (depth_image_view_)
    context_.device.destroyImageView(depth_image_view_);
  if (image_available_semaphore_)
    context_.device.destroySemaphore(image_available_semaphore_);
  if (render_finished_semaphore_)
    context_.device.destroySemaphore(render_finished_semaphore_);
}

vk::Result SwapchainManager::Init() {
  vk::SemaphoreCreateInfo info;
  auto result1 = context_.device.createSemaphore(info);
  auto result2 = context_.device.createSemaphore(info);
  image_available_semaphore_ = result1.value;
  render_finished_semaphore_ = result2.value;
  ESCHER_LOG_VK_ERROR(result1.result,
                      "failed to create 'image available' semaphore");
  ESCHER_LOG_VK_ERROR(result2.result,
                      "failed to create 'image available' semaphore");
  // Return success if both are successful.
  return result1.result == vk::Result::eSuccess ? result2.result
                                                : result1.result;
}

void SwapchainManager::SetSwapchain(const VulkanSwapchain& swapchain) {
  bool dimensions_changed = false;
  if (swapchain_.width != swapchain.width ||
      swapchain_.height != swapchain.height) {
    dimensions_changed = true;
  }

  swapchain_ = swapchain;
  swapchain_index_ = UINT32_MAX;  // invalid

  // If the width or height change, we need to create a new depth/stencil
  // image.
  if (dimensions_changed) {
    // Need to create a new depth/stencil buffer.
    if (depth_image_view_) {
      context_.device.destroyImageView(depth_image_view_);
    }

    width_ = swapchain_.width;
    height_ = swapchain_.height;
    depth_image_ = image_cache_->GetDepthImage(depth_format_, width_, height_);

    vk::ImageViewCreateInfo info;
    info.viewType = vk::ImageViewType::e2D;
    info.format = depth_format_;
    info.subresourceRange.aspectMask =
        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    info.image = depth_image_->image();
    depth_image_view_ =
        ESCHER_CHECKED_VK_RESULT(context_.device.createImageView(info));
  }

  // Generate framebuffers corresponding to each swapchain image.
  for (auto& fb : framebuffers_) {
    context_.device.destroyFramebuffer(fb);
  }
  framebuffers_.clear();
  framebuffers_.reserve(swapchain_.image_views.size());
  for (auto& view : swapchain_.image_views) {
    std::array<vk::ImageView, 2> attachments;
    attachments[0] = view;
    attachments[1] = depth_image_view_;

    vk::FramebufferCreateInfo info;
    info.renderPass = render_pass_;
    info.attachmentCount = static_cast<uint32_t>(attachments.size());
    info.pAttachments = attachments.data();
    info.width = width_;
    info.height = height_;
    info.layers = 1;

    auto fb = ESCHER_CHECKED_VK_RESULT(context_.device.createFramebuffer(info));
    framebuffers_.push_back(fb);
  }
}

SwapchainManager::FrameInfoResult SwapchainManager::BeginFrame() {
  FTL_DCHECK(!in_frame_);

  auto result = context_.device.acquireNextImageKHR(
      swapchain_.swapchain, UINT64_MAX, image_available_semaphore_, nullptr);

  if (result.result == vk::Result::eSuboptimalKHR) {
    FTL_DLOG(WARNING) << "suboptimal swapchain configuration";
  } else if (result.result != vk::Result::eSuccess) {
    FTL_LOG(WARNING) << "failed to acquire next swapchain image"
                     << " : " << to_string(result.result);
    frame_info_ = FrameInfo();
    return FrameInfoResult(result.result, frame_info_);
  }

  in_frame_ = true;
  swapchain_index_ = result.value;
  frame_info_.framebuffer = framebuffers_[swapchain_index_];
  frame_info_.image_available_semaphore = image_available_semaphore_;
  frame_info_.render_finished_semaphore = render_finished_semaphore_;
  return FrameInfoResult(vk::Result::eSuccess, frame_info_);
}

void SwapchainManager::EndFrame() {
  FTL_DCHECK(in_frame_);

  in_frame_ = false;
  frame_info_ = FrameInfo();

  // When the image is rendered, present it.
  vk::PresentInfoKHR info;
  info.waitSemaphoreCount = 1;
  info.pWaitSemaphores = &render_finished_semaphore_;
  info.swapchainCount = 1;
  info.pSwapchains = &swapchain_.swapchain;
  info.pImageIndices = &swapchain_index_;
  ESCHER_LOG_VK_ERROR(context_.queue.presentKHR(info),
                      "failed to present rendered image");
}

}  // namespace impl
}  // namespace escher
