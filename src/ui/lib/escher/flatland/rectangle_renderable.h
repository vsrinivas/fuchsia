// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_FLATLAND_RECTANGLE_RENDERABLE_H_
#define SRC_UI_LIB_ESCHER_FLATLAND_RECTANGLE_RENDERABLE_H_

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace escher {

// Struct representing the region of an image
// that a rectangle covers. Each of the four
// rectangle corners are explicitly listed,
// with the default values covering the whole
// texture with no rotation. Any rotations on
// the rectangle can be done implicitly by
// changing the uv coordinates here. Since the
// rectangles are always axis-aligned, only
// rotations that are multiples of 90 degrees
// are supported.
struct RectangleSourceSpec {
  vec2 uv_top_left = vec2(0, 0);
  vec2 uv_top_right = vec2(1, 0);
  vec2 uv_bottom_right = vec2(1, 1);
  vec2 uv_bottom_left = vec2(0, 1);
};

// Struct representing a rectangle renderable's
// dimensions on a screen. The origin represents
// the top-left-hand corner and the extent is the
// width and height. Values are given in pixels.
struct RectangleDestinationSpec {
  vec2 origin = vec2(0, 0);
  vec2 extent = vec2(0, 0);
};

// Struct representing a complete Rectangle Renderable.
// It contains both source and destination specs, a
// texture, a multiply color, and bool for transparency.
struct RectangleRenderable {
  RectangleSourceSpec source;
  RectangleDestinationSpec dest;

  // Renderer never holds onto this pointer.
  Texture* texture = nullptr;
  vec4 color = vec4(1, 1, 1, 1);

  // If this bool is false, the renderable will render
  // as if it is opaque, even if its color or texture
  // has an alpha value less than 1.
  bool is_transparent = false;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_FLATLAND_RECTANGLE_RENDERABLE_H_
