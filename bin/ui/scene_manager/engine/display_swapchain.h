// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/engine/swapchain.h"

#include "escher/vk/vulkan_swapchain.h"

namespace scene_manager {

class Display;
class EventTimestamper;

// DisplaySwapchain implements the Swapchain interface by using a Vulkan
// swapchain to present images to a physical display.
class DisplaySwapchain : public Swapchain {
 public:
  DisplaySwapchain(Display* display,
                   EventTimestamper* timestamper,
                   escher::Escher* escher,
                   escher::VulkanSwapchain swapchain);
  ~DisplaySwapchain() override;

  // |Swapchain|
  bool DrawAndPresentFrame(const FrameTimingsPtr& frame_timings,
                           DrawCallback draw_callback) override;

 private:
  Display* const display_;
  EventTimestamper* const event_timestamper_;
  escher::VulkanSwapchain swapchain_;
  vk::Device device_;
  vk::Queue queue_;

  size_t next_semaphore_index_ = 0;

  std::vector<escher::SemaphorePtr> image_available_semaphores_;
  std::vector<escher::SemaphorePtr> render_finished_semaphores_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplaySwapchain);
};

}  // namespace scene_manager
