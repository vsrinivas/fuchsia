// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_SHAPE_ROUNDED_RECT_H_
#define LIB_ESCHER_SHAPE_ROUNDED_RECT_H_

#include <cstdint>
#include <utility>

#include "lib/escher/geometry/types.h"

namespace escher {

struct MeshSpec;

// Specify a rounded-rect that is centered at (0,0).
struct RoundedRectSpec {
  // Note: radii are in clockwise order, starting from top-left.
  RoundedRectSpec(float width, float height, float top_left_radius,
                  float top_right_radius, float bottom_right_radius,
                  float bottom_left_radius);

  float width;
  float height;
  float top_left_radius;
  float top_right_radius;
  float bottom_right_radius;
  float bottom_left_radius;

  bool ContainsPoint(vec2 point) const;
};

// Return the number of vertices and indices that are required to tessellate the
// specified rounded-rect.  The first element of the pair is the vertex count,
// and the second element is the index count.
std::pair<uint32_t, uint32_t> GetRoundedRectMeshVertexAndIndexCounts(
    const RoundedRectSpec& spec);

void GenerateRoundedRectIndices(const RoundedRectSpec& spec,
                                const MeshSpec& mesh_spec, void* indices_out,
                                uint32_t max_bytes);

void GenerateRoundedRectVertices(const RoundedRectSpec& spec,
                                 const MeshSpec& mesh_spec, void* vertices_out,
                                 uint32_t max_bytes);

}  // namespace escher

#endif  // LIB_ESCHER_SHAPE_ROUNDED_RECT_H_
