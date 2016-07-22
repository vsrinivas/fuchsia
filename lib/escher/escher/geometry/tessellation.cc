// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/geometry/tessellation.h"

#include <math.h>

#include "ftl/logging.h"

namespace escher {

void Tessellation::SanityCheck() const {
  FTL_DCHECK(!positions.empty());
  FTL_DCHECK(normals.empty() || normals.size() == positions.size());
  FTL_DCHECK(uvs.empty() || uvs.size() == positions.size());
  FTL_DCHECK(!indices.empty());
  size_t vertex_count = positions.size();
  FTL_DCHECK(std::all_of(indices.begin(), indices.end(),
      [vertex_count](GLuint index) { return index < vertex_count; }));
}

Tessellation TessellateCircle(int subdivisions, vec2 center, float radius) {
  // Compute the number of vertices in the tessellated circle.
  FTL_DCHECK(subdivisions >= 0);
  int circle_vertex_count = 4;
  while (subdivisions-- > 0)
      circle_vertex_count *= 2;

  // Reserve space for the result, and for intermediate computations.
  Tessellation result;
  result.positions.reserve(circle_vertex_count);
  result.indices.reserve(circle_vertex_count * 3 - 6);
  std::vector<GLushort> circle_indices;
  std::vector<GLushort> half_of_circle_indices;
  circle_indices.reserve(circle_vertex_count);
  half_of_circle_indices.reserve(circle_vertex_count / 2);

  // Generate vertex positions, and indices that will be consumed below.
  const float radian_step = 2 * M_PI / circle_vertex_count;
  for (GLuint i = 0; i < circle_vertex_count; ++i) {
    float radians = i * radian_step;
    float x = sin(radians) * radius + center.x;
    float y = cos(radians) * radius + center.y;
    result.positions.push_back(vec3(x, y, 0.0f));
    circle_indices.push_back(i);
  }

  // Tesselate outer edge of circle, working inward.  Every second vertex
  // becomes the outer point of a triangle, and is not made available to the
  // next iteration.  As a result, each successive iteration has half as many
  // indices to process, exactly as if 'subdivisions - 1' had been passed as
  // the argument to this function.
  while (circle_indices.size() > 2) {
    for (GLuint i = 0; i < circle_indices.size(); i += 2) {
      result.indices.push_back(circle_indices[i]);
      result.indices.push_back(circle_indices[(i + 1) % circle_indices.size()]);
      result.indices.push_back(circle_indices[(i + 2) % circle_indices.size()]);

      // Keep half of the indices (every second one) for the next iteration.
      half_of_circle_indices.push_back(circle_indices[i]);
    }
    std::swap(circle_indices, half_of_circle_indices);
    half_of_circle_indices.clear();
  }
  FTL_DCHECK(circle_indices.size() == 2);
  FTL_DCHECK(result.positions.size() == circle_vertex_count);
  FTL_DCHECK(result.indices.size() == circle_vertex_count * 3 - 6);

  return result;
}

}  // namespace escher
