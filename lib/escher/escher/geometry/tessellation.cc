// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/geometry/tessellation.h"

#include <algorithm>
#include <math.h>

#include "escher/impl/model_data.h"
#include "escher/shape/mesh_builder.h"
#include "escher/shape/mesh_builder_factory.h"
#include "ftl/logging.h"

namespace escher {

Tessellation::Tessellation() {}
Tessellation::~Tessellation() {}

void Tessellation::SanityCheck() const {
  FTL_DCHECK(!positions.empty());
  FTL_DCHECK(normals.empty() || normals.size() == positions.size());
  FTL_DCHECK(uvs.empty() || uvs.size() == positions.size());
  FTL_DCHECK(!indices.empty());
  size_t vertex_count = positions.size();
  FTL_DCHECK(std::all_of(
      indices.begin(), indices.end(),
      [vertex_count](size_t index) { return index < vertex_count; }));
}

MeshPtr TessellateCircle(MeshBuilderFactory* factory,
                         const MeshSpec& spec,
                         int subdivisions,
                         vec2 center,
                         float radius) {
  // Compute the number of vertices in the tessellated circle.
  FTL_DCHECK(subdivisions >= 0);
  size_t circle_vertex_count = 4;
  while (subdivisions-- > 0)
    circle_vertex_count *= 2;

  auto builder = factory->NewMeshBuilder(spec, circle_vertex_count,
                                         circle_vertex_count * 3 - 6);

  {
    // TODO: Only pos/color vertices are currently supported.
    MeshSpec supported_spec;
    supported_spec.flags |= MeshAttributeFlagBits::kPosition;
    supported_spec.flags |= MeshAttributeFlagBits::kUV;
    FTL_DCHECK(spec == supported_spec);
  }
  impl::ModelData::PerVertex vertex{vec2(0.0, 0.0), vec2(0.0, 0.0)};

  // Generate vertex positions.
  const float radian_step = 2 * M_PI / circle_vertex_count;
  for (size_t i = 0; i < circle_vertex_count; ++i) {
    float radians = i * radian_step;
    float x = sin(radians) * radius + center.x;
    float y = cos(radians) * radius + center.y;
    float u = sin(radians) * 0.5f + 0.5f;
    float v = cos(radians) * 0.5f + 0.5f;
    vertex.position = vec2(x, y);
    vertex.uv = vec2(u, v);
    builder->AddVertex(vertex);
  }

  // Generate vertex indices.
  // Tesselate outer edge of circle, working inward.  Every second vertex
  // becomes the outer point of a triangle, and is not made available to the
  // next iteration.  As a result, each successive iteration has half as many
  // indices to process, exactly as if 'subdivisions - 1' had been passed as
  // the argument to this function.
  std::vector<uint16_t> circle_indices;
  std::vector<uint16_t> half_of_circle_indices;
  circle_indices.reserve(circle_vertex_count);
  half_of_circle_indices.reserve(circle_vertex_count / 2);
  for (size_t i = 0; i < circle_vertex_count; ++i) {
    circle_indices.push_back(i);
  }
  while (circle_indices.size() > 2) {
    for (size_t i = 0; i < circle_indices.size(); i += 2) {
      builder->AddIndex(circle_indices[(i + 1) % circle_indices.size()]);
      builder->AddIndex(circle_indices[i]);
      builder->AddIndex(circle_indices[(i + 2) % circle_indices.size()]);

      // Keep half of the indices (every second one) for the next iteration.
      half_of_circle_indices.push_back(circle_indices[i]);
    }
    std::swap(circle_indices, half_of_circle_indices);
    half_of_circle_indices.clear();
  }
  FTL_DCHECK(circle_indices.size() == 2);

  auto mesh = builder->Build();
  FTL_DCHECK(mesh->num_indices == circle_vertex_count * 3 - 6);
  return mesh;
}

}  // namespace escher
