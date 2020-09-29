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
const vk::ImageUsageFlags RectangleCompositor::kRenderTargetUsageFlags =
    vk::ImageUsageFlagBits::eColorAttachment;
const vk::ImageUsageFlags RectangleCompositor::kTextureUsageFlags =
    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;

namespace {

vec4 GetPremultipliedRgba(vec4 rgba) { return vec4(vec3(rgba) * rgba.a, rgba.a); }

// Draws a single rectangle at a particular depth value, z.
void DrawSingle(CommandBuffer* cmd_buf, const Rectangle2D& rectangle, const Texture* texture,
                const glm::vec4& color, float z) {
  TRACE_DURATION("gfx", "RectangleCompositor::DrawSingle");

  // Bind texture to use in the fragment shader.
  cmd_buf->BindTexture(/*set*/ 0, /*binding*/ 0, texture);

  // Struct to store all the push constant data in the vertex shader
  // so we only need to make a single call to PushConstants().
  struct VertexShaderPushConstants {
    alignas(16) vec3 origin;
    alignas(8) vec2 extent;
    alignas(8) std::array<vec2, 4> uvs;
  };

  // Set up the push constants struct with data from the renderable and z value.
  VertexShaderPushConstants constants = {
      .origin = vec3(rectangle.origin, z),
      .extent = rectangle.extent,
      .uvs = rectangle.clockwise_uvs,
  };

  // We offset by 16U to account for the fact that the previous call to
  // PushConstants() for the batch-level bounds was a glm::vec3, which
  // takes up 16 bytes with padding in the vertex shader.
  cmd_buf->PushConstants(constants, /*offset*/ 16U);

  // We make one more call to PushConstants() to push the color to the
  // fragment shader. This is so that the data aligns with the push constant
  // range for the fragment shader only, otherwise it would overlap the ranges
  // for both the vertex and fragment shaders.
  cmd_buf->PushConstants(GetPremultipliedRgba(color), /*offset*/ 80U);

  // Draw two triangles. The vertex shader knows how to use the gl_VertexIndex
  // of each vertex to compute the appropriate position and UV values.
  cmd_buf->Draw(/*vertex_count*/ 6);
}

// Renders the batch of provided rectangles using the provided shader program.
// Renderables are separated into opaque and translucent groups. The opaque
// renderables are rendered from front-to-back while the translucent renderables
// are rendered from back-to-front.
void TraverseBatch(CommandBuffer* cmd_buf, vec3 bounds, ShaderProgramPtr program,
                   const std::vector<Rectangle2D>& rectangles,
                   const std::vector<const TexturePtr>& textures,
                   const std::vector<RectangleCompositor::ColorData>& color_data) {
  TRACE_DURATION("gfx", "RectangleCompositor::TraverseBatch");
  int64_t num_renderables = static_cast<int64_t>(rectangles.size());

  // Set the shader program to be used.
  cmd_buf->SetShaderProgram(program, nullptr);

  // Push the bounds as a constant for all renderables to be used in the vertex shader.
  cmd_buf->PushConstants(bounds);

  // Opaque, front to back.
  {
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);
    cmd_buf->SetDepthTestAndWrite(true, true);

    float z = 1.f;
    for (int64_t i = num_renderables - 1; i >= 0; i--) {
      if (!color_data[i].is_transparent) {
        DrawSingle(cmd_buf, rectangles[i], textures[i].get(), color_data[i].color, z);
      }
      z += 1.f;
    }
  }

  // Translucent, back to front.
  {
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kTranslucent);
    cmd_buf->SetDepthTestAndWrite(true, false);
    float z = static_cast<float>(rectangles.size());
    for (int64_t i = 0; i < num_renderables; i++) {
      if (color_data[i].is_transparent) {
        DrawSingle(cmd_buf, rectangles[i], textures[i].get(), color_data[i].color, z);
      }
      z -= 1.f;
    }
  }
}

}  // anonymous namespace

// RectangleCompositor constructor. Initializes the shader program and allocates
// GPU buffers to store mesh data.
RectangleCompositor::RectangleCompositor(Escher* escher)
    : standard_program_(escher->GetProgram(kFlatlandStandardProgram)) {}

// DrawBatch generates the Vulkan data needed to render the batch (e.g. renderpass,
// bounds, etc) and calls |TraverseBatch| which iterates over the renderables and
// submits them for rendering.
void RectangleCompositor::DrawBatch(CommandBuffer* cmd_buf,
                                    const std::vector<Rectangle2D>& rectangles,
                                    const std::vector<const TexturePtr>& textures,
                                    const std::vector<ColorData>& color_data,
                                    const ImagePtr& output_image, const TexturePtr& depth_buffer) {
  // TODO (fxr/43278): Add custom clear colors. We could either pass in another parameter to
  // this function or try to embed clear-data into the existing api. For example, one could
  // check to see if the back rectangle is fullscreen and solid-color, in which case we can
  // treat it as a clear instead of rendering it as a renderable.
  FX_CHECK(cmd_buf && output_image && depth_buffer);

  // Inputs need to be the same length.
  FX_CHECK(rectangles.size() == textures.size());
  FX_CHECK(rectangles.size() == color_data.size());

  // Initialize the render pass.
  RenderPassInfo render_pass;
  vk::Rect2D render_area = {{0, 0}, {output_image->width(), output_image->height()}};

  if (!RenderPassInfo::InitRenderPassInfo(&render_pass, render_area, output_image, depth_buffer)) {
    FX_LOGS(ERROR) << "RectangleCompositor::DrawBatch(): RenderPassInfo initialization failed. "
                      "Exiting.";
    return;
  }

  // Construct the bounds that are used in the vertex shader to convert the
  // renderable positions into normalized device coordinates (NDC). The width
  // and height are divided by 2 to pre-optimize the shift that happens in the
  // shader which realigns the NDC coordinates so that (0,0) is in the center
  // instead of in the top-left-hand corner.
  vec3 bounds(static_cast<float>(output_image->width() * 0.5),
              static_cast<float>(output_image->height() * 0.5), rectangles.size());

  // Start the render pass.
  cmd_buf->BeginRenderPass(render_pass);

  // Iterate over all the renderables and draw them.
  TraverseBatch(cmd_buf, bounds, standard_program_, rectangles, textures, color_data);

  // End the render pass.
  cmd_buf->EndRenderPass();
}

vk::ImageCreateInfo RectangleCompositor::GetDefaultImageConstraints(const vk::Format& vk_format,
                                                                    vk::ImageUsageFlags usage) {
  vk::ImageCreateInfo create_info;
  create_info.imageType = vk::ImageType::e2D;
  create_info.extent = vk::Extent3D{1, 1, 1};
  create_info.flags = {};
  create_info.format = vk_format;
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  create_info.samples = vk::SampleCountFlagBits::e1;
  create_info.tiling = vk::ImageTiling::eOptimal;
  create_info.usage = usage;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;
  return create_info;
}

}  // namespace escher
