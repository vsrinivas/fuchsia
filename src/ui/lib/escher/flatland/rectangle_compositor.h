// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_FLATLAND_RECTANGLE_COMPOSITOR_H_
#define SRC_UI_LIB_ESCHER_FLATLAND_RECTANGLE_COMPOSITOR_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/vk/shader_program.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace escher {

// |RectangleCompositor| provides an interface for rendering
// axis-aligned rectangles in 2D space, as part of the
// "Flatland" API.
class RectangleCompositor {
 public:
  static const vk::ImageUsageFlags kRenderTargetUsageFlags;
  static const vk::ImageUsageFlags kTextureUsageFlags;

  struct ColorData {
    ColorData(vec4 in_color, bool in_opaque) : color(in_color), is_opaque(in_opaque) {
      FX_CHECK(glm::all(glm::greaterThanEqual(in_color, vec4(0.f))));
      FX_CHECK(glm::all(glm::lessThanEqual(in_color, vec4(1.f))));
    }

    const vec4 color = vec4(1.f);
    const bool is_opaque = false;
  };

  explicit RectangleCompositor(Escher* escher);
  ~RectangleCompositor() = default;

  // Draws a single batch of renderables into the provided output image.
  // Parameters:
  // - cmd_buf: The command buffer used to record commands.
  // - rectangles: geometry to be drawn.
  // - textures: must be 1-1 with rectangles, to which they are textured onto.
  // - color_data: must be 1-1 with rectangles and textures.
  //             |color| is multiply_color to the texture used in the shader.
  //             |is_opaque| determines use of opaque or transparent rendering.
  // - output_image: the render target the renderables will be rendered into.
  // - depth_buffer: The depth texture to be used for z-buffering.
  //
  // Depth is implicit. Renderables are drawn in the order they appear in the input
  // vector, with the first entry being the furthest back, and the last the closest.
  void DrawBatch(CommandBuffer* cmd_buf, const std::vector<Rectangle2D>& rectangles,
                 const std::vector<const TexturePtr>& textures,
                 const std::vector<ColorData>& color_data, const ImagePtr& output_image,
                 const TexturePtr& depth_buffer);

  // Minimal image constraints to be set on textures passed into DrawBatch.
  static vk::ImageCreateInfo GetDefaultImageConstraints(const vk::Format& vk_format,
                                                        vk::ImageUsageFlags usage);

 private:
  RectangleCompositor(const RectangleCompositor&) = delete;

  // Default shader program that all renderables use.
  ShaderProgramPtr standard_program_ = nullptr;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_FLATLAND_RECTANGLE_COMPOSITOR_H_
