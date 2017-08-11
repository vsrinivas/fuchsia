// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/vk/vulkan_swapchain.h"

namespace scene_manager {

// Swapchain is an interface used used to render into an escher::Image and
// present the result (to a physical display or elsewhere).
class Swapchain {
 public:
  // The three arguments are:
  // - the framebuffer to render into.
  // - the semaphore to wait upon before rendering into the framebuffer
  // - the semaphore to signal when rendering is complete.
  using DrawCallback = std::function<void(const escher::ImagePtr&,
                                          const escher::SemaphorePtr&,
                                          const escher::SemaphorePtr&)>;

  // Returns false if the frame could not be drawn.
  virtual bool DrawAndPresentFrame(DrawCallback draw_callback) = 0;
};

// DisplaySwapchain implements the Swapchain interface by using a Vulkan
// swapchain to present images to a physical display.
class DisplaySwapchain : public Swapchain {
 public:
  DisplaySwapchain(escher::Escher* escher, escher::VulkanSwapchain swapchain);
  ~DisplaySwapchain();

  bool DrawAndPresentFrame(DrawCallback draw_callback) override;

 private:
  escher::VulkanSwapchain swapchain_;
  vk::Device device_;
  vk::Queue queue_;

  size_t next_semaphore_index_ = 0;
  std::vector<escher::SemaphorePtr> image_available_semaphores_;
  std::vector<escher::SemaphorePtr> render_finished_semaphores_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DisplaySwapchain);
};

}  // namespace scene_manager
