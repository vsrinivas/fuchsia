// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene_manager/renderer/renderer.h"

#include "escher/vk/vulkan_swapchain_helper.h"

namespace mozart {
namespace scene {

class DisplayRenderer final : public Renderer {
 public:
  DisplayRenderer(Session* session,
                  ResourceId id,
                  FrameScheduler* frame_scheduler,
                  escher::PaperRendererPtr paper_renderer,
                  escher::VulkanSwapchain swapchain);

  ~DisplayRenderer();

 private:
  // |Renderer|
  virtual void DrawFrame() override;

  escher::PaperRendererPtr paper_renderer_;
  escher::VulkanSwapchainHelper swapchain_helper_;
};

}  // namespace scene
}  // namespace mozart
