// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flatland/rectangle_renderable.h"

#include "src/lib/fxl/logging.h"

namespace escher {

bool RectangleRenderable::IsValid(const RectangleRenderable& renderable,
                                  bool ignore_texture_for_testing) {
  // All renderables need a valid texture.
  if (!ignore_texture_for_testing && !renderable.texture) {
    FXL_LOG(WARNING) << "Renderable texture is null.";
    return false;
  }

  // Make sure the multiple color's channels are each in the range [0,1].
  if (!glm::all(glm::greaterThanEqual(renderable.color, vec4(0.f)))) {
    FXL_LOG(WARNING) << "Renderable color has channel < 0: " << renderable.color;
    return false;
  }
  if (!glm::all(glm::lessThanEqual(renderable.color, vec4(1.f)))) {
    FXL_LOG(WARNING) << "Renderable color has channel > 1: " << renderable.color;
    return false;
  }

  // Make sure each component of each UV coordinate is in the range [0,1].
  for (uint32_t i = 0; i < 4; i++) {
    if (!glm::all(
            glm::greaterThanEqual(renderable.source.uv_coordinates_clockwise[i], vec2(0.f)))) {
      return false;
    }
    if (!glm::all(glm::lessThanEqual(renderable.source.uv_coordinates_clockwise[i], vec2(1.f)))) {
      return false;
    }
  }

  // Make sure that the extent coordinates are non-negative.
  if (!glm::all(glm::greaterThanEqual(renderable.dest.extent, vec2(0.f)))) {
    return false;
  }

  // Rectangle is valid!
  return true;
}

void RectangleRenderable::Rotate(RectangleRenderable* renderable, uint32_t degrees) {
  FXL_DCHECK(renderable);
  FXL_DCHECK(degrees % 90 == 0);

  // Make sure degrees are in the range [0, 360].
  degrees = degrees % 360;

  switch (degrees) {
    // Do nothing.
    case 0:
      break;
    case 90: {
      auto uvs = renderable->source.uv_coordinates_clockwise;
      renderable->source.uv_top_left = uvs[3];
      renderable->source.uv_top_right = uvs[0];
      renderable->source.uv_bottom_right = uvs[1];
      renderable->source.uv_bottom_left = uvs[2];
      std::swap(renderable->dest.extent.x, renderable->dest.extent.y);
      break;
    }
    case 180: {
      auto uvs = renderable->source.uv_coordinates_clockwise;
      renderable->source.uv_top_left = uvs[2];
      renderable->source.uv_top_right = uvs[3];
      renderable->source.uv_bottom_right = uvs[0];
      renderable->source.uv_bottom_left = uvs[1];
      break;
    }
    case 270: {
      auto uvs = renderable->source.uv_coordinates_clockwise;
      renderable->source.uv_top_left = uvs[1];
      renderable->source.uv_top_right = uvs[2];
      renderable->source.uv_bottom_right = uvs[3];
      renderable->source.uv_bottom_left = uvs[0];
      std::swap(renderable->dest.extent.x, renderable->dest.extent.y);
      break;
    }
  }
}

void RectangleRenderable::FlipHorizontally(RectangleRenderable* renderable) {
  FXL_DCHECK(renderable);
  std::swap(renderable->source.uv_top_left, renderable->source.uv_top_right);
  std::swap(renderable->source.uv_bottom_left, renderable->source.uv_bottom_right);
}

void RectangleRenderable::FlipVertically(RectangleRenderable* renderable) {
  FXL_DCHECK(renderable);
  std::swap(renderable->source.uv_top_left, renderable->source.uv_bottom_left);
  std::swap(renderable->source.uv_top_right, renderable->source.uv_bottom_right);
}

}  // namespace escher
