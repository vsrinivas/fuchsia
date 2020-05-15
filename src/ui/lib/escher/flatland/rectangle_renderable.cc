// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flatland/rectangle_renderable.h"

#include <lib/syslog/cpp/macros.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace escher {

namespace {
static const float kRadiansToDegrees = 180.0 / glm::pi<float>();

// Helper function for ensuring that two vectors are equal while taking into
// account floating point discrepancies via an epsilon term.
bool Equal(const glm::vec2 a, const glm::vec2 b) {
  return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec2(0.001f)));
}
}  // namespace

bool RectangleRenderable::IsValid(const RectangleRenderable& renderable,
                                  bool ignore_texture_for_testing) {
  // All renderables need a valid texture.
  if (!ignore_texture_for_testing && !renderable.texture) {
    FX_LOGS(WARNING) << "Renderable texture is null.";
    return false;
  }

  // Make sure the multiple color's channels are each in the range [0,1].
  if (!glm::all(glm::greaterThanEqual(renderable.color, vec4(0.f)))) {
    FX_LOGS(WARNING) << "Renderable color has channel < 0: " << renderable.color;
    return false;
  }
  if (!glm::all(glm::lessThanEqual(renderable.color, vec4(1.f)))) {
    FX_LOGS(WARNING) << "Renderable color has channel > 1: " << renderable.color;
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

const RectangleRenderable RectangleRenderable::Create(const glm::mat3& matrix,
                                                      const ClockwiseUVs& uvs, Texture* texture,
                                                      const glm::vec4& color, bool is_transparent) {
  // The local-space of the renderable has its top-left origin point at (0,0) and grows
  // downward and to the right, so that the bottom-right point is at (1,-1). We apply
  // the matrix to the four points that represent this unit square to get the points
  // in the global coordinate space.
  const glm::vec2 verts[4] = {
      matrix * glm::vec3(0, 0, 1),
      matrix * glm::vec3(1, 0, 1),
      matrix * glm::vec3(1, -1, 1),
      matrix * glm::vec3(0, -1, 1),
  };

  float min_x = FLT_MAX, min_y = FLT_MAX;
  float max_x = -FLT_MAX, max_y = -FLT_MAX;
  for (uint32_t i = 0; i < 4; i++) {
    min_x = std::min(min_x, verts[i].x);
    min_y = std::min(min_y, verts[i].y);
    max_x = std::max(max_x, verts[i].x);
    max_y = std::max(max_y, verts[i].y);
  }

  glm::vec2 reordered_verts[4] = {
      glm::vec2(min_x, max_y),  // top_left
      glm::vec2(max_x, max_y),  // top_right
      glm::vec2(max_x, min_y),  // bottom_right
      glm::vec2(min_x, min_y),  // bottom_left
  };

  ClockwiseUVs reordered_uvs;
  for (uint32_t i = 0; i < 4; i++) {
    for (uint32_t j = 0; j < 4; j++) {
      if (Equal(reordered_verts[i], verts[j])) {
        reordered_uvs[i] = uvs[j];
        break;
      }
    }
  }

  RectangleRenderable renderable = {
      .source.uv_coordinates_clockwise = reordered_uvs,
      .dest.origin = reordered_verts[0],
      .dest.extent = reordered_verts[1] - reordered_verts[3],
      .texture = texture,
      .color = color,
      .is_transparent = is_transparent,
  };
  FX_DCHECK(RectangleRenderable::IsValid(renderable, /*ignore texture*/ true));
  return renderable;
}

}  // namespace escher
