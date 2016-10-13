// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vulkan/vulkan.hpp>

#include "escher/vk/vulkan_context.h"
#include "ftl/macros.h"

namespace escher {
namespace impl {

// Manages creation and caching of render-passes and pipelines for Escher's
// Renderer subclasses.
class RenderPassManager {
 public:
  explicit RenderPassManager(const VulkanContext& context);
  ~RenderPassManager();

  vk::RenderPass GetPaperRendererRenderPass();

 private:
  vk::RenderPass CreatePaperRendererRenderPass();

  // Destroy |render_pass| if it is not null.
  void DestroyRenderPass(vk::RenderPass render_pass);

  VulkanContext context_;
  vk::RenderPass paper_renderer_render_pass_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RenderPassManager);
};

}  // namespace impl
}  // namespace escher
