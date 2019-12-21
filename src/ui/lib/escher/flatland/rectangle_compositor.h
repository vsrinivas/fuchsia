// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_FLATLAND_RECTANGLE_COMPOSITOR_H_
#define SRC_UI_LIB_ESCHER_FLATLAND_RECTANGLE_COMPOSITOR_H_

#include "src/ui/lib/escher/flatland/rectangle_renderable.h"
#include "src/ui/lib/escher/forward_declarations.h"

namespace escher {

// |RectangleCompositor| provides an interface for rendering
// axis-aligned rectangles in 2D space, as part of the
// "Flatland" API.
class RectangleCompositor {
 public:
  static std::unique_ptr<RectangleCompositor> New(EscherWeakPtr escher) {
    return std::make_unique<RectangleCompositor>(std::move(escher));
  }

  explicit RectangleCompositor(EscherWeakPtr escher);
  ~RectangleCompositor() = default;

  // Draws a single batch of renderables into the provided output image.
  // Parameters:
  // - cmd_buf: The command buffer used to record commands.
  // - renderables: the batch of renderables to be drawn.
  // - output_image: the render target the renderables will be rendered into.
  // - depth_buffer: The depth texture to be used for z-buffering.
  //
  // Depth is implicit. Renderables are drawn in the order they appear in the input
  // vector, with the first entry being the furthest back, and the last the closest.
  void DrawBatch(CommandBuffer* cmd_buf, const std::vector<RectangleRenderable>& renderables,
                 const ImagePtr& output_image, const TexturePtr& depth_buffer);

 private:
  RectangleCompositor(const RectangleCompositor&) = delete;

  // Default shader program that all renderables use.
  ShaderProgramPtr standard_program_ = nullptr;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_FLATLAND_RECTANGLE_COMPOSITOR_H_
