// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene_manager/renderer/renderer.h"

#include "escher/vk/vulkan_swapchain_helper.h"

namespace scene_manager {

class DisplayRenderer final : public Renderer {
 public:
  // Any swapchain that uses PaperRenderer must be a multiple of this many
  // pixels.
  static const uint32_t kRequiredSwapchainPixelMultiple;

  DisplayRenderer(Session* session,
                  mozart::ResourceId id,
                  escher::PaperRendererPtr paper_renderer,
                  escher::VulkanSwapchain swapchain);

  ~DisplayRenderer();

 private:
  // |Renderer|
  virtual void DrawFrame() override;

  escher::PaperRendererPtr paper_renderer_;
  escher::VulkanSwapchainHelper swapchain_helper_;
};

}  // namespace scene_manager
