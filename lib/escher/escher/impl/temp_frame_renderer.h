// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/render_context.h"

namespace escher {
namespace impl {

// Temporary hack to render something on the screen.
class TempFrameRenderer {
 public:
  TempFrameRenderer(const VulkanContext& context, vk::RenderPass render_pass);
  ~TempFrameRenderer();

  vk::Result Render(RenderContext::Frame* frame, vk::Framebuffer framebuffer);

 private:
  VulkanContext context_;
  vk::RenderPass render_pass_;
};

}  // namespace impl
}  // namespace escher
