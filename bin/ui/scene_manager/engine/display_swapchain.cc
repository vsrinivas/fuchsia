// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/engine/display_swapchain.h"

#include <trace/event.h>

#include "garnet/bin/ui/scene_manager/displays/display.h"
#include "garnet/bin/ui/scene_manager/engine/frame_timings.h"
#include "garnet/bin/ui/scene_manager/util/escher_utils.h"

#include "escher/escher.h"

namespace scene_manager {

DisplaySwapchain::DisplaySwapchain(Display* display,
                                   EventTimestamper* timestamper,
                                   escher::Escher* escher,
                                   escher::VulkanSwapchain swapchain)
    : display_(display),
      event_timestamper_(timestamper),
      swapchain_(std::move(swapchain)),
      device_(escher->vk_device()),
      queue_(escher->device()->vk_main_queue()) {
  display_->Claim();

  image_available_semaphores_.reserve(swapchain_.images.size());
  render_finished_semaphores_.reserve(swapchain_.images.size());
  for (size_t i = 0; i < swapchain_.images.size(); ++i) {
// TODO: Use timestamper to listen for event notifications
#if 1
    image_available_semaphores_.push_back(escher::Semaphore::New(device_));
    render_finished_semaphores_.push_back(escher::Semaphore::New(device_));
#else
    auto pair = NewSemaphoreEventPair(escher);
    image_available_semaphores_.push_back(std::move(pair.first));
    watches_.push_back(
        timestamper, std::move(pair.second), kFenceSignalled,
        [this, i](zx_time_t timestamp) { OnFramePresented(i, timestamp); });
#endif
  }
}

// TODO(MZ-142): We should manage the lifetime of the swapchain object, and
// destroy it here.  However, we currently obtain the swapchain from the
// escher::DemoHarness that eventually destroys it.
DisplaySwapchain::~DisplaySwapchain() {
  display_->Unclaim();
}

bool DisplaySwapchain::DrawAndPresentFrame(const FrameTimingsPtr& frame_timings,
                                           DrawCallback draw_callback) {
  // TODO(MZ-260): replace Vulkan swapchain with Magma C ABI calls, and use
  // EventTimestamper::Wait to notify |frame| when the frame is finished
  // rendering, and when it is presented.
  // auto timing_index = frame->AddSwapchain(this);
  if (event_timestamper_ && !event_timestamper_) {
    // Avoid unused-variable error.
    FXL_CHECK(false) << "I don't believe you.";
  }

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
      FXL_DLOG(WARNING) << "suboptimal swapchain configuration";
    } else if (result.result != vk::Result::eSuccess) {
      FXL_LOG(WARNING) << "failed to acquire next swapchain image"
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
    FXL_DCHECK(false) << "DisplaySwapchain::DrawAndPresentFrame(): failed to "
                         "present rendered image.";
  }
  return true;
}

}  // namespace scene_manager
