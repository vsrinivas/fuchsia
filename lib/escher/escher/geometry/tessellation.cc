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

struct VertexAttributePointers {
  vec2* pos = nullptr;
  vec2* uv = nullptr;
  vec2* pos_offset = nullptr;
  float* perim = nullptr;
};

// Get pointers to each of the supported vertex attributes within the
// memory pointed to by |vertex|. This is based on the attributes' offsets
// (looked up in the MeshBuilder).
// If the |MeshSpec| does not include an attribute, its corresponding pointer
// will be null.
VertexAttributePointers GetVertexAttributePointers(uint8_t* vertex,
                                                   size_t vertex_size,
                                                   const MeshSpec& spec,
                                                   MeshBuilderPtr builder) {
  FTL_CHECK(builder->vertex_stride() <= vertex_size);

  VertexAttributePointers attribute_pointers{};

  // Compute the offset of each vertex attribute.  While we're at it, set the
  // values for the circle's center vertex.
  if (spec.flags & MeshAttribute::kPosition) {
    attribute_pointers.pos = reinterpret_cast<vec2*>(
        vertex + builder->GetAttributeOffset(MeshAttribute::kPosition));
  }
  if (spec.flags & MeshAttribute::kUV) {
    attribute_pointers.uv = reinterpret_cast<vec2*>(
        vertex + builder->GetAttributeOffset(MeshAttribute::kUV));
  }
  if (spec.flags & MeshAttribute::kPositionOffset) {
    attribute_pointers.pos_offset = reinterpret_cast<vec2*>(
        vertex + builder->GetAttributeOffset(MeshAttribute::kPositionOffset));
  }
  if (spec.flags & MeshAttribute::kPerimeterPos) {
    attribute_pointers.perim = reinterpret_cast<float*>(
        vertex + builder->GetAttributeOffset(MeshAttribute::kPerimeterPos));
  }
  return attribute_pointers;
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
  auto vertex_p =
      GetVertexAttributePointers(vertex, kMaxVertexSize, spec, builder);

  // Build center vertex.
  if (vertex_p.pos)
    (*vertex_p.pos) = center;
  if (vertex_p.uv)
    (*vertex_p.uv) = vec2(0.5f, 0.5f);
  if (vertex_p.pos_offset)
    (*vertex_p.pos_offset) = vec2(0.f, 0.f);
  // TODO: This is an undesirable singularity.  Perhaps it would be better to
  // treat circles as a ring with inner radius of zero?
  if (vertex_p.perim)
    (*vertex_p.perim) = 0.f;
  builder->AddVertexData(vertex, builder->vertex_stride());

  // Outer vertices.
  const float outer_vertex_count_reciprocal = 1.f / outer_vertex_count;
  const float radian_step = 2 * M_PI / outer_vertex_count;
  for (size_t i = 0; i < outer_vertex_count; ++i) {
    float radians = i * radian_step;

    // Direction of the current vertex from the center of the circle.
    vec2 dir(sin(radians), cos(radians));

    if (vertex_p.pos)
      (*vertex_p.pos) = dir * radius + center;
    if (vertex_p.uv)
      (*vertex_p.uv) = 0.5f * (dir + vec2(1.f, 1.f));
    if (vertex_p.pos_offset)
      (*vertex_p.pos_offset) = dir * offset_magnitude;
    if (vertex_p.perim)
      (*vertex_p.perim) = i * outer_vertex_count_reciprocal;

    builder->AddVertexData(vertex, builder->vertex_stride());
  }

  // Vertex indices.
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
  auto vertex_p =
      GetVertexAttributePointers(vertex, kMaxVertexSize, spec, builder);

  const float outer_vertex_count_reciprocal = 1.f / outer_vertex_count;
  const float radian_step = 2 * M_PI / outer_vertex_count;
  for (size_t i = 0; i < outer_vertex_count; ++i) {
    float radians = i * radian_step;

    // Direction of the current vertex from the center of the circle.
    vec2 dir(sin(radians), cos(radians));

    // Build outer-ring vertex.
    if (vertex_p.pos)
      (*vertex_p.pos) = dir * outer_radius + center;
    if (vertex_p.uv)
      (*vertex_p.uv) = 0.5f * (dir + vec2(1.f, 1.f));
    if (vertex_p.pos_offset)
      (*vertex_p.pos_offset) = dir * outer_offset_magnitude;
    if (vertex_p.perim)
      (*vertex_p.perim) = i * outer_vertex_count_reciprocal;
    builder->AddVertexData(vertex, builder->vertex_stride());

    // Build inner-ring vertex.  Only the position and offset may differ from
    // the corresponding outer-ring vertex.
    if (vertex_p.pos) {
      (*vertex_p.pos) = dir * inner_radius + center;
    }
    if (vertex_p.pos_offset) {
      // Positive offsets point inward, toward the center of the circle.
      (*vertex_p.pos_offset) = dir * -inner_offset_magnitude;
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

MeshPtr NewSimpleRectangleMesh(MeshBuilderFactory* factory) {
  MeshSpec spec{MeshAttribute::kPosition | MeshAttribute::kUV};

  // In each vertex, the first two floats represent the position and the second
  // two are UV coordinates.
  vec4 v0(0.f, 0.f, 0.f, 0.f);
  vec4 v1(1.f, 0.f, 1.f, 0.f);
  vec4 v2(1.f, 1.f, 1.f, 1.f);
  vec4 v3(0.f, 1.f, 0.f, 1.f);

  MeshBuilderPtr builder = factory->NewMeshBuilder(spec, 4, 6);
  return builder->AddVertex(v0)
      .AddVertex(v1)
      .AddVertex(v2)
      .AddVertex(v3)
      .AddIndex(0)
      .AddIndex(1)
      .AddIndex(2)
      .AddIndex(0)
      .AddIndex(2)
      .AddIndex(3)
      .Build();
}

MeshPtr NewRectangleMesh(MeshBuilderFactory* factory,
                         const MeshSpec& spec,
                         int subdivisions,
                         vec2 size,
                         vec2 top_left,
                         float top_offset_magnitude,
                         float bottom_offset_magnitude) {
  // Compute the number of vertices in the tessellated circle.
  FTL_DCHECK(subdivisions >= 0);
  size_t vertices_per_side = 2;
  while (subdivisions-- > 0) {
    vertices_per_side *= 2;
  }

  size_t vertex_count = vertices_per_side * 2;
  size_t index_count = (vertices_per_side - 1) * 6;

  auto builder = factory->NewMeshBuilder(spec, vertex_count, index_count);

  // Generate vertex positions.
  constexpr size_t kMaxVertexSize = 100;
  uint8_t vertex[kMaxVertexSize];
  auto vertex_p =
      GetVertexAttributePointers(vertex, kMaxVertexSize, spec, builder);

  const float vertices_per_side_reciprocal = 1.f / (vertices_per_side - 1);
  for (size_t i = 0; i < vertices_per_side; ++i) {
    // Build bottom vertex.
    if (vertex_p.pos)
      (*vertex_p.pos) =
          top_left + vec2(size.x * i * vertices_per_side_reciprocal, size.y);
    if (vertex_p.uv)
      (*vertex_p.uv) = vec2(i * vertices_per_side_reciprocal, 1.f);
    if (vertex_p.pos_offset)
      (*vertex_p.pos_offset) = vec2(0, 1.f * bottom_offset_magnitude);
    if (vertex_p.perim)
      (*vertex_p.perim) = i * vertices_per_side_reciprocal;
    builder->AddVertexData(vertex, builder->vertex_stride());

    // Build top vertex.
    if (vertex_p.pos)
      (*vertex_p.pos) =
          top_left + vec2(size.x * i * vertices_per_side_reciprocal, 0);
    if (vertex_p.uv)
      (*vertex_p.uv) = vec2(i * vertices_per_side_reciprocal, 0);
    if (vertex_p.pos_offset)
      (*vertex_p.pos_offset) = vec2(0, -1.f * top_offset_magnitude);
    if (vertex_p.perim)
      (*vertex_p.perim) = i * vertices_per_side_reciprocal;
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

  auto mesh = builder->Build();
  FTL_DCHECK(mesh->num_indices == index_count);
  return mesh;
}

MeshPtr NewFullScreenMesh(MeshBuilderFactory* factory) {
  MeshSpec spec{MeshAttribute::kPosition | MeshAttribute::kUV};

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
