// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flatland/rectangle_compositor.h"

#include "src/ui/lib/escher/flatland/flatland_static_config.h"
#include "src/ui/lib/escher/mesh/indexed_triangle_mesh_upload.h"
#include "src/ui/lib/escher/mesh/tessellation.h"
#include "src/ui/lib/escher/renderer/render_funcs.h"
#include "src/ui/lib/escher/vk/shader_program.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace escher {
namespace {

// Draws a single renderable at a particular depth value, z.
void DrawSingle(CommandBuffer* cmd_buf, const RectangleRenderable& renderable, const float& z) {
  TRACE_DURATION("gfx", "RectangleCompositor::DrawSingle");

  // Bind texture.
  FXL_DCHECK(renderable.texture);
  cmd_buf->BindTexture(/*set*/ 0, /*binding*/ 0, renderable.texture);

  // Struct to store all the push constant data so we only need
  // to make a single call to PushConstants().
  struct PushConstants {
    alignas(16) vec3 origin;
    alignas(8) vec2 extent;
    alignas(8) vec2 uvs[4];
    alignas(16) vec4 color;
  };

  // Set up the push constants struct with data from the renderable and z value.
  PushConstants constants = {
      .origin = vec3(renderable.dest.origin, z),
      .extent = renderable.dest.extent,
      .uvs = {renderable.source.uv_top_left, renderable.source.uv_top_right,
              renderable.source.uv_bottom_right, renderable.source.uv_bottom_left},
      .color = renderable.color,

  };

  // We offset by 16U to account for the fact that the previous call to
  // PushConstants() for the batch-level bounds was a glm::vec3, which
  // takes up 16 bytes with padding in the vertex shader.
  cmd_buf->PushConstants(constants, /*offset*/ 16U);

  // Draw two triangles. The vertex shader knows how to use the gl_VertexIndex
  // of each vertex to compute the appropriate position and UV values.
  cmd_buf->Draw(/*vertex_count*/ 6);
}

// Renders the batch of RectangleRenderables using the provided shader program.
// Renderables are separated into opaque and translucent groups. The opaque
// renderables are rendered from front-to-back while the translucent renderables
// are rendered from back-to-front.
void TraverseBatch(CommandBuffer* cmd_buf, vec3 bounds, ShaderProgramPtr program,
                   const std::vector<RectangleRenderable>& renderables) {
  TRACE_DURATION("gfx", "RectangleCompositor::TraverseBatch");

  // Set the shader program to be used.
  cmd_buf->SetShaderProgram(program, nullptr);

  // Push the bounds as a constant for all renderables to be used in the vertex shader.
  cmd_buf->PushConstants(bounds);

  // Opaque, front to back.
  {
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);
    cmd_buf->SetDepthTestAndWrite(true, true);

    float z = 1.f;
    auto it = renderables.rbegin();  // rbegin is reverse iterator.
    while (it != renderables.rend()) {
      if (!it->is_transparent) {
        DrawSingle(cmd_buf, *it, z);
      }
      ++it;
      z += 1.f;
    }
  }

  // Translucent, back to front.
  {
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kTranslucent);
    cmd_buf->SetDepthTestAndWrite(true, false);
    float z = static_cast<float>(renderables.size());
    for (const auto& renderable : renderables) {
      if (renderable.is_transparent) {
        DrawSingle(cmd_buf, renderable, z);
      }
      z -= 1.f;
    }
  }
}

}  // anonymous namespace

// RectangleCompositor constructor. Initializes the shader program and allocates
// GPU buffers to store mesh data.
RectangleCompositor::RectangleCompositor(EscherWeakPtr weak_escher)
    : standard_program_(weak_escher->GetProgram(kFlatlandStandardProgram)) {}

// DrawBatch generates the Vulkan data needed to render the batch (e.g. renderpass,
// bounds, etc) and calls |TraverseBatch| which iterates over the renderables and
// submits them for rendering.
void RectangleCompositor::DrawBatch(CommandBuffer* cmd_buf,
                                    const std::vector<RectangleRenderable>& renderables,
                                    const ImagePtr& output_image, const TexturePtr& depth_buffer) {
  // TODO (fxr/43278): Add custom clear colors. We could either pass in another parameter to
  // this function or try to embed clear-data into the existing api. For example, one could
  // check to see if the back rectangle is fullscreen and solid-color, in which case we can
  // treat it as a clear instead of rendering it as a renderable.
  FXL_DCHECK(cmd_buf && output_image && depth_buffer);

  // Initialize the render pass.
  RenderPassInfo render_pass;
  vk::Rect2D render_area = {{0, 0}, {output_image->width(), output_image->height()}};
  RenderPassInfo::InitRenderPassInfo(&render_pass, render_area, output_image, depth_buffer);

  // Construct the bounds that are used in the vertex shader to convert the
  // renderable positions into normalized device coordinates (NDC). The width
  // and height are divided by 2 to pre-optimize the shift that happens in the
  // shader which realigns the NDC coordinates so that (0,0) is in the center
  // instead of in the top-left-hand corner.
  vec3 bounds(output_image->width() * 0.5f, output_image->height() * 0.5f, renderables.size());

  // Start the render pass.
  cmd_buf->BeginRenderPass(render_pass);

  // Iterate over all the renderables and draw them.
  TraverseBatch(cmd_buf, bounds, standard_program_, renderables);

  // End the render pass.
  cmd_buf->EndRenderPass();
}

}  // namespace escher
