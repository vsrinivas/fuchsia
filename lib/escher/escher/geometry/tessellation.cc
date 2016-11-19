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

MeshPtr NewCircleMesh(MeshBuilderFactory* factory,
                      const MeshSpec& spec,
                      int subdivisions,
                      vec2 center,
                      float radius,
                      float offset_magnitude) {
  // Compute the number of vertices in the tessellated circle.
  FTL_DCHECK(subdivisions >= 0);
  size_t outer_vertex_count = 4;
  while (subdivisions-- > 0) {
    outer_vertex_count *= 2;
  }

  size_t vertex_count = outer_vertex_count + 1;  // Add 1 for center vertex.
  size_t index_count = outer_vertex_count * 3;

  auto builder = factory->NewMeshBuilder(spec, vertex_count, index_count);

  // Generate vertex positions.
  constexpr size_t kMaxVertexSize = 100;
  uint8_t vertex[kMaxVertexSize];
  FTL_CHECK(builder->vertex_stride() <= kMaxVertexSize);

  vec2* pos = nullptr;
  vec2* uv = nullptr;
  vec2* pos_offset = nullptr;
  float* perim = nullptr;

  // Compute the offset of each vertex attribute.  While we're at it, set the
  // values for the circle's center vertex.
  if (spec.flags & MeshAttributeFlagBits::kPosition) {
    pos = reinterpret_cast<vec2*>(
        vertex + builder->GetAttributeOffset(MeshAttributeFlagBits::kPosition));
    *pos = center;
  }
  if (spec.flags & MeshAttributeFlagBits::kUV) {
    uv = reinterpret_cast<vec2*>(
        vertex + builder->GetAttributeOffset(MeshAttributeFlagBits::kUV));
    *uv = vec2(0.5f, 0.5f);
  }
  if (spec.flags & MeshAttributeFlagBits::kPositionOffset) {
    pos_offset = reinterpret_cast<vec2*>(
        vertex +
        builder->GetAttributeOffset(MeshAttributeFlagBits::kPositionOffset));
    *pos_offset = vec2(0.f, 0.f);
  }
  if (spec.flags & MeshAttributeFlagBits::kPerimeter) {
    perim = reinterpret_cast<float*>(
        vertex +
        builder->GetAttributeOffset(MeshAttributeFlagBits::kPerimeter));
    // TODO: This is an undesirable singularity.  Perhaps it would be better to
    // treat circles as a ring with inner radius of zero?
    *perim = 0.f;
  }
  builder->AddVertexData(vertex, builder->vertex_stride());

  // Compute attributes for each of the circle's outer vertices.
  const float outer_vertex_count_reciprocal = 1.f / outer_vertex_count;
  const float radian_step = 2 * M_PI / outer_vertex_count;
  for (size_t i = 0; i < outer_vertex_count; ++i) {
    float radians = i * radian_step;

    // Direction of the current vertex from the center of the circle.
    vec2 dir(sin(radians), cos(radians));

    if (pos) {
      *pos = dir * radius + center;
    }
    if (uv) {
      *uv = 0.5f * (dir + vec2(1.f, 1.f));
    }
    if (pos_offset) {
      *pos_offset = dir * offset_magnitude;
    }
    if (perim) {
      *perim = i * outer_vertex_count_reciprocal;
    }

    builder->AddVertexData(vertex, builder->vertex_stride());
  }

  // Generate vertex indices.
  for (size_t i = 1; i < outer_vertex_count; ++i) {
    builder->AddIndex(0);
    builder->AddIndex(i + 1);
    builder->AddIndex(i);
  }
  builder->AddIndex(0);
  builder->AddIndex(1);
  builder->AddIndex(outer_vertex_count);

  auto mesh = builder->Build();
  FTL_DCHECK(mesh->num_indices == index_count);
  return mesh;
}

MeshPtr NewRingMesh(MeshBuilderFactory* factory,
                    const MeshSpec& spec,
                    int subdivisions,
                    vec2 center,
                    float outer_radius,
                    float inner_radius,
                    float outer_offset_magnitude,
                    float inner_offset_magnitude) {
  // Compute the number of vertices in the tessellated circle.
  FTL_DCHECK(subdivisions >= 0);
  size_t outer_vertex_count = 4;
  while (subdivisions-- > 0) {
    outer_vertex_count *= 2;
  }

  size_t vertex_count = outer_vertex_count * 2;
  size_t index_count = outer_vertex_count * 6;

  auto builder = factory->NewMeshBuilder(spec, vertex_count, index_count);

  // Generate vertex positions.
  constexpr size_t kMaxVertexSize = 100;
  uint8_t vertex[kMaxVertexSize];
  FTL_CHECK(builder->vertex_stride() <= kMaxVertexSize);

  vec2* pos = nullptr;
  vec2* uv = nullptr;
  vec2* pos_offset = nullptr;
  float* perim = nullptr;

  if (spec.flags & MeshAttributeFlagBits::kPosition) {
    pos = reinterpret_cast<vec2*>(
        vertex + builder->GetAttributeOffset(MeshAttributeFlagBits::kPosition));
  }
  if (spec.flags & MeshAttributeFlagBits::kUV) {
    uv = reinterpret_cast<vec2*>(
        vertex + builder->GetAttributeOffset(MeshAttributeFlagBits::kUV));
  }
  if (spec.flags & MeshAttributeFlagBits::kPositionOffset) {
    pos_offset = reinterpret_cast<vec2*>(
        vertex +
        builder->GetAttributeOffset(MeshAttributeFlagBits::kPositionOffset));
  }
  if (spec.flags & MeshAttributeFlagBits::kPerimeter) {
    perim = reinterpret_cast<float*>(
        vertex +
        builder->GetAttributeOffset(MeshAttributeFlagBits::kPerimeter));
  }

  const float outer_vertex_count_reciprocal = 1.f / outer_vertex_count;
  const float radian_step = 2 * M_PI / outer_vertex_count;
  for (size_t i = 0; i < outer_vertex_count; ++i) {
    float radians = i * radian_step;

    // Direction of the current vertex from the center of the circle.
    vec2 dir(sin(radians), cos(radians));

    // Build outer-ring vertex.
    if (pos) {
      *pos = dir * outer_radius + center;
    }
    if (uv) {
      *uv = 0.5f * (dir + vec2(1.f, 1.f));
    }
    if (pos_offset) {
      *pos_offset = dir * outer_offset_magnitude;
    }
    if (perim) {
      *perim = i * outer_vertex_count_reciprocal;
    }
    builder->AddVertexData(vertex, builder->vertex_stride());

    // Build inner-ring vertex.  Only the position and offset may differ from
    // the corresponding outer-ring vertex.
    if (pos) {
      *pos = dir * inner_radius + center;
    }
    if (pos_offset) {
      // Positive offsets point inward, toward the center of the circle.
      *pos_offset = dir * -inner_offset_magnitude;
    }
    builder->AddVertexData(vertex, builder->vertex_stride());
  }

  // Generate vertex indices.
  for (size_t i = 2; i < vertex_count; i += 2) {
    builder->AddIndex(i - 2);
    builder->AddIndex(i - 1);
    builder->AddIndex(i);
    builder->AddIndex(i);
    builder->AddIndex(i - 1);
    builder->AddIndex(i + 1);
  }
  builder->AddIndex(vertex_count - 2);
  builder->AddIndex(vertex_count - 1);
  builder->AddIndex(0);
  builder->AddIndex(0);
  builder->AddIndex(vertex_count - 1);
  builder->AddIndex(1);

  auto mesh = builder->Build();
  FTL_DCHECK(mesh->num_indices == index_count);
  return mesh;
}

MeshPtr NewFullScreenMesh(MeshBuilderFactory* factory) {
  MeshSpec spec;
  spec.flags = MeshAttributeFlagBits::kPosition | MeshAttributeFlagBits::kUV;

  // Some internet lore has it that it is better to use a single triangle rather
  // than a rectangle composed of a pair of triangles, so that is what we do.
  // The triangle extends beyond the bounds of the screen, and is clipped so
  // that each fragment has the same position and UV coordinates as would a
  // two-triangle quad. In each vertex, the first two coordinates are position,
  // and the second two are UV coords.
  return factory->NewMeshBuilder(spec, 3, 3)
      ->AddVertex(vec4(-1.f, -1.f, 0.f, 0.f))
      .AddVertex(vec4(3.f, -1.f, 2.f, 0.f))
      .AddVertex(vec4(-1.f, 3.f, 0.f, 2.f))
      .AddIndex(0)
      .AddIndex(1)
      .AddIndex(2)
      .Build();
}

}  // namespace escher
