// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/vulkan_swapchain.h"

#include "apps/tracing/lib/trace/event.h"
#include "escher/escher.h"

namespace scene_manager {

DisplaySwapchain::DisplaySwapchain(escher::Escher* escher,
                                   escher::VulkanSwapchain swapchain)
    : swapchain_(std::move(swapchain)),
      device_(escher->device()->vk_device()),
      queue_(escher->device()->vk_main_queue()) {
  image_available_semaphores_.reserve(swapchain_.images.size());
  render_finished_semaphores_.reserve(swapchain_.images.size());
  for (size_t i = 0; i < swapchain_.images.size(); ++i) {
    image_available_semaphores_.push_back(escher::Semaphore::New(device_));
    render_finished_semaphores_.push_back(escher::Semaphore::New(device_));
  }
}

// TODO(MZ-142): We should manage the lifetime of the swapchain object, and
// destroy it here.  However, we currently obtain the swapchain from the
// escher::DemoHarness that eventually destroys it.
DisplaySwapchain::~DisplaySwapchain() = default;

bool DisplaySwapchain::DrawAndPresentFrame(DrawCallback draw_callback) {
  auto& image_available_semaphore =
      image_available_semaphores_[next_semaphore_index_];
  auto& render_finished_semaphore =
      render_finished_semaphores_[next_semaphore_index_];

  uint32_t swapchain_index;
  {
    TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() acquire");

    auto result = device_.acquireNextImageKHR(
        swapchain_.swapchain, UINT64_MAX, image_available_semaphore->value(),
        nullptr);

    if (result.result == vk::Result::eSuboptimalKHR) {
      FTL_DLOG(WARNING) << "suboptimal swapchain configuration";
    } else if (result.result != vk::Result::eSuccess) {
      FTL_LOG(WARNING) << "failed to acquire next swapchain image"
                       << " : " << to_string(result.result);
      return false;
    }

    swapchain_index = result.value;
    next_semaphore_index_ =
        (next_semaphore_index_ + 1) % swapchain_.images.size();
  }

  // Render the scene.  The Renderer will wait for acquireNextImageKHR() to
  // signal the semaphore.
  draw_callback(swapchain_.images[swapchain_index], image_available_semaphore,
                render_finished_semaphore);

  // When the image is completely rendered, present it.
  TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() present");
  vk::PresentInfoKHR info;
  info.waitSemaphoreCount = 1;
  auto sema = render_finished_semaphore->value();
  info.pWaitSemaphores = &sema;
  info.swapchainCount = 1;
  info.pSwapchains = &swapchain_.swapchain;
  info.pImageIndices = &swapchain_index;

  // TODO(MZ-244): handle this more robustly.
  if (queue_.presentKHR(info) != vk::Result::eSuccess) {
    FTL_DCHECK(false) << "DisplaySwapchain::DrawAndPresentFrame(): failed to "
                         "present rendered image.";
  }
  return true;
}

}  // namespace scene_manager
