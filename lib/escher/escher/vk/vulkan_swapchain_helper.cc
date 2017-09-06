// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/vk/vulkan_swapchain_helper.h"

#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/framebuffer.h"
#include "escher/renderer/paper_renderer.h"
#include "escher/scene/camera.h"
#include "escher/scene/stage.h"
#include "escher/util/trace_macros.h"

namespace escher {

VulkanSwapchainHelper::VulkanSwapchainHelper(VulkanSwapchain swapchain,
                                             vk::Device device,
                                             vk::Queue queue)
    : swapchain_(swapchain), device_(device), queue_(queue) {
  for (size_t i = 0; i < swapchain_.images.size(); ++i) {
    image_available_semaphores_.push_back(Semaphore::New(device_));
    render_finished_semaphores_.push_back(Semaphore::New(device_));
  }
}

VulkanSwapchainHelper::~VulkanSwapchainHelper() {}

void VulkanSwapchainHelper::DrawFrame(PaperRenderer* renderer,
                                      const Stage& stage,
                                      const Model& model,
                                      const Camera& camera,
                                      const Model* overlay_model) {
  FTL_DCHECK(renderer);

  auto& image_available_semaphore =
      image_available_semaphores_[next_semaphore_index_];
  auto& render_finished_semaphore =
      render_finished_semaphores_[next_semaphore_index_];

  uint32_t swapchain_index;
  {
    TRACE_DURATION("gfx", "escher::VulkanSwapchain::Acquire");

    auto result = device_.acquireNextImageKHR(
        swapchain_.swapchain, UINT64_MAX, image_available_semaphore->value(),
        nullptr);

    if (result.result == vk::Result::eSuboptimalKHR) {
      FTL_DLOG(WARNING) << "suboptimal swapchain configuration";
    } else if (result.result != vk::Result::eSuccess) {
      FTL_LOG(WARNING) << "failed to acquire next swapchain image"
                       << " : " << to_string(result.result);
      return;
    }

    swapchain_index = result.value;
    next_semaphore_index_ =
        (next_semaphore_index_ + 1) % swapchain_.images.size();
  }

  // Render the scene.  The Renderer will wait for acquireNextImageKHR() to
  // signal the semaphore.
  auto& image = swapchain_.images[swapchain_index];
  image->SetWaitSemaphore(image_available_semaphore);
  renderer->DrawFrame(stage, model, camera, image, overlay_model,
                      render_finished_semaphore, nullptr);

  // When the image is completely rendered, present it.
  TRACE_DURATION("gfx", "escher::VulkanSwapchain::Present");
  vk::PresentInfoKHR info;
  info.waitSemaphoreCount = 1;
  auto sema = render_finished_semaphore->value();
  info.pWaitSemaphores = &sema;
  info.swapchainCount = 1;
  info.pSwapchains = &swapchain_.swapchain;
  info.pImageIndices = &swapchain_index;
  ESCHER_LOG_VK_ERROR(queue_.presentKHR(info),
                      "failed to present rendered image");
}

}  // namespace escher
