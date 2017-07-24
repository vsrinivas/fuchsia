// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene_manager/resources/renderers/renderer.h"

#include "apps/mozart/src/scene_manager/displays/display.h"
#include "escher/vk/vulkan_swapchain_helper.h"

namespace scene_manager {

class DisplayRenderer final : public Renderer {
 public:
  // Any swapchain that uses PaperRenderer must be a multiple of this many
  // pixels.
  static const uint32_t kRequiredSwapchainPixelMultiple;

  DisplayRenderer(Session* session,
                  mozart::ResourceId id,
                  Display* display,
                  escher::VulkanSwapchain swapchain);

  ~DisplayRenderer();

 private:
  // |Renderer|
  virtual void DrawFrame(escher::Renderer* renderer) override;

  Display* const display_;
  escher::VulkanSwapchainHelper swapchain_helper_;
};

}  // namespace scene_manager
