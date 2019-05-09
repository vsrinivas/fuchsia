// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/vulkan_swapchain_helper.h"

#include <chrono>
#include <thread>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/framebuffer.h"

namespace escher {

VulkanSwapchainHelper::VulkanSwapchainHelper(VulkanSwapchain swapchain,
                                             vk::Device device, vk::Queue queue)
    : swapchain_(swapchain), device_(device), queue_(queue) {
  for (size_t i = 0; i < swapchain_.images.size(); ++i) {
    image_available_semaphores_.push_back(Semaphore::New(device_));
    render_finished_semaphores_.push_back(Semaphore::New(device_));
  }
}

VulkanSwapchainHelper::~VulkanSwapchainHelper() {}

void VulkanSwapchainHelper::DrawFrame(DrawFrameCallback draw_callback) {
  auto& image_available_semaphore =
      image_available_semaphores_[next_semaphore_index_];
  auto& render_finished_semaphore =
      render_finished_semaphores_[next_semaphore_index_];

  uint32_t swapchain_index;
  {
    TRACE_DURATION("gfx", "escher::VulkanSwapchain::Acquire");

    auto result = device_.acquireNextImageKHR(
        swapchain_.swapchain, UINT64_MAX,
        image_available_semaphore->vk_semaphore(), nullptr);

    if (result.result == vk::Result::eSuboptimalKHR) {
      FXL_DLOG(WARNING) << "suboptimal swapchain configuration";
    } else if (result.result == vk::Result::eTimeout) {
      int retry_count = 0;
      int timeout = 2;
      while (result.result == vk::Result::eTimeout) {
        TRACE_DURATION("gfx", "escher::VulkanSwapchain::Acquire[retry]");
        if (retry_count++ > 10) {
          FXL_LOG(WARNING) << "failed to acquire next swapchain image"
                           << " : Timeout (giving up after 10 tries)";
          return;
        }

        std::chrono::duration<double, std::milli> chrono_timeout(timeout);
        std::this_thread::sleep_for(chrono_timeout);
        timeout *= 2;

        device_.waitIdle();

        result = device_.acquireNextImageKHR(
            swapchain_.swapchain, UINT64_MAX,
            image_available_semaphore->vk_semaphore(), nullptr);
      }

    } else if (result.result != vk::Result::eSuccess) {
      FXL_LOG(WARNING) << "failed to acquire next swapchain image"
                       << " : " << to_string(result.result);
      return;
    }

    swapchain_index = result.value;
    next_semaphore_index_ =
        (next_semaphore_index_ + 1) % swapchain_.images.size();
  }

  // Render the scene.  The Renderer will wait for acquireNextImageKHR() to
  // signal the semaphore.
  auto& color_image_out = swapchain_.images[swapchain_index];
  color_image_out->SetWaitSemaphore(image_available_semaphore);
  draw_callback(color_image_out, render_finished_semaphore);

  // When the image is completely rendered, present it.
  TRACE_DURATION("gfx", "escher::VulkanSwapchain::Present");
  vk::PresentInfoKHR info;
  info.waitSemaphoreCount = 1;
  auto sema = render_finished_semaphore->vk_semaphore();
  info.pWaitSemaphores = &sema;
  info.swapchainCount = 1;
  info.pSwapchains = &swapchain_.swapchain;
  info.pImageIndices = &swapchain_index;
  ESCHER_LOG_VK_ERROR(queue_.presentKHR(info),
                      "failed to present rendered image");
}

}  // namespace escher
