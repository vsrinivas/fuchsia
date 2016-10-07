// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vulkan/vulkan.hpp>

#include "escher/geometry/types.h"
#include "escher/impl/glsl_compiler.h"
#include "escher/impl/render_context.h"

namespace escher {
namespace impl {

// Temporary hack to render something on the screen.
class TempFrameRenderer {
 public:
  TempFrameRenderer(const VulkanContext& context,
                    MeshManager* mesh_manager,
                    vk::RenderPass render_pass);
  ~TempFrameRenderer();

  vk::Result Render(RenderContext::Frame* frame, vk::Framebuffer framebuffer);

 private:
  MeshPtr CreateTriangle();

  VulkanContext context_;
  MeshManager* mesh_manager_;
  vk::RenderPass render_pass_;

  MeshPtr triangle_;

  vk::PipelineLayout pipeline_layout_;
  vk::Pipeline pipeline_;

  GlslToSpirvCompiler compiler_;
};

}  // namespace impl
}  // namespace escher
