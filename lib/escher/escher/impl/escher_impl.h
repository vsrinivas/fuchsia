// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/escher.h"
#include "escher/impl/render_context.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"
#include "escher/vk/vulkan_context.h"
#include "ftl/macros.h"

namespace escher {
namespace impl {

class EscherImpl {
 public:
  EscherImpl(const VulkanContext& context, const VulkanSwapchain& swapchain);
  ~EscherImpl();

  // Public API methods.  See escher.h
  Status Render(const Stage& stage, const Model& model);
  void SetSwapchain(const VulkanSwapchain& swapchain);

 private:
  RenderContext render_context_;
  bool device_lost_ = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(EscherImpl);
};

}  // namespace impl
}  // namespace escher
