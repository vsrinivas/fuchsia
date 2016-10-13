// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/vk/vulkan_swapchain_helper.h"

#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/framebuffer.h"
#include "escher/renderer/renderer.h"

namespace escher {

VulkanSwapchainHelper::VulkanSwapchainHelper(VulkanSwapchain swapchain,
                                             const RendererPtr& renderer)
    : swapchain_(swapchain),
      renderer_(renderer),
      device_(renderer->vulkan_context().device),
      queue_(renderer->vulkan_context().queue) {
  framebuffers_.reserve(swapchain.images.size());
  for (auto& im : swapchain.images) {
    framebuffers_.push_back(renderer->NewFramebuffer(im));
  }
  image_available_semaphore_ = Semaphore::New(device_);
  render_finished_semaphore_ = Semaphore::New(device_);
}

VulkanSwapchainHelper::~VulkanSwapchainHelper() {}

void VulkanSwapchainHelper::DrawFrame(Stage& stage, Model& model) {
  auto result =
      device_.acquireNextImageKHR(swapchain_.swapchain, UINT64_MAX,
                                  image_available_semaphore_->value(), nullptr);

  if (result.result == vk::Result::eSuboptimalKHR) {
    FTL_DLOG(WARNING) << "suboptimal swapchain configuration";
  } else if (result.result != vk::Result::eSuccess) {
    FTL_LOG(WARNING) << "failed to acquire next swapchain image"
                     << " : " << to_string(result.result);
    return;
  }
  uint32_t swapchain_index = result.value;

  // Render the scene.  The Renderer will wait for acquireNextImageKHR() to
  // signal the semaphore.
  auto& framebuffer = framebuffers_[swapchain_index];
  framebuffer->SetWaitSemaphore(image_available_semaphore_);
  renderer_->DrawFrame(stage, model, framebuffer, render_finished_semaphore_,
                       nullptr);

  // When the image is completely rendered, present it.
  vk::PresentInfoKHR info;
  info.waitSemaphoreCount = 1;
  auto sema = render_finished_semaphore_->value();
  info.pWaitSemaphores = &sema;
  info.swapchainCount = 1;
  info.pSwapchains = &swapchain_.swapchain;
  info.pImageIndices = &swapchain_index;
  ESCHER_LOG_VK_ERROR(queue_.presentKHR(info),
                      "failed to present rendered image");
}

}  // namespace escher
