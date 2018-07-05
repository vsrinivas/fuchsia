// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/geometry/tessellation.h"

#include <math.h>
#include <algorithm>

#include "lib/escher/impl/model_data.h"
#include "lib/escher/shape/mesh_builder.h"
#include "lib/escher/shape/mesh_builder_factory.h"
#include "lib/fxl/logging.h"

namespace escher {

struct VertexAttributePointers {
  vec2* pos2 = nullptr;
  vec3* pos3 = nullptr;
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
  FXL_CHECK(builder->vertex_stride() <= vertex_size);
  FXL_DCHECK(spec.IsValid());

  VertexAttributePointers attribute_pointers{};

  // Compute the offset of each vertex attribute.
  if (spec.flags & MeshAttribute::kPosition2D) {
    attribute_pointers.pos2 = reinterpret_cast<vec2*>(
        vertex + spec.GetAttributeOffset(MeshAttribute::kPosition2D));
  }
  if (spec.flags & MeshAttribute::kPosition3D) {
    attribute_pointers.pos3 = reinterpret_cast<vec3*>(
        vertex + spec.GetAttributeOffset(MeshAttribute::kPosition3D));
  }
  if (spec.flags & MeshAttribute::kUV) {
    attribute_pointers.uv = reinterpret_cast<vec2*>(
        vertex + spec.GetAttributeOffset(MeshAttribute::kUV));
  }
  if (spec.flags & MeshAttribute::kPositionOffset) {
    attribute_pointers.pos_offset = reinterpret_cast<vec2*>(
        vertex + spec.GetAttributeOffset(MeshAttribute::kPositionOffset));
  }
  if (spec.flags & MeshAttribute::kPerimeterPos) {
    attribute_pointers.perim = reinterpret_cast<float*>(
        vertex + spec.GetAttributeOffset(MeshAttribute::kPerimeterPos));
  }
  return attribute_pointers;
}

MeshPtr NewCircleMesh(MeshBuilderFactory* factory, const MeshSpec& spec,
                      int subdivisions, vec2 center, float radius,
                      float offset_magnitude) {
  // Compute the number of vertices in the tessellated circle.
  FXL_DCHECK(subdivisions >= 0);
  FXL_DCHECK(spec.IsValid());
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
  FXL_CHECK(vertex_p.pos2);
  (*vertex_p.pos2) = center;
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

    (*vertex_p.pos2) = dir * radius + center;
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
  FXL_DCHECK(mesh->num_indices() == index_count);
  FXL_DCHECK(mesh->bounding_box() ==
             BoundingBox(vec3(center.x - radius, center.y - radius, 0),
                         vec3(center.x + radius, center.y + radius, 0)));
  return mesh;
}

MeshPtr NewRingMesh(MeshBuilderFactory* factory, const MeshSpec& spec,
                    int subdivisions, vec2 center, float outer_radius,
                    float inner_radius, float outer_offset_magnitude,
                    float inner_offset_magnitude) {
  // Compute the number of vertices in the tessellated circle.
  FXL_DCHECK(subdivisions >= 0);
  FXL_DCHECK(spec.IsValid());
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
  FXL_CHECK(vertex_p.pos2);

  const float outer_vertex_count_reciprocal = 1.f / outer_vertex_count;
  const float radian_step = 2 * M_PI / outer_vertex_count;
  for (size_t i = 0; i < outer_vertex_count; ++i) {
    float radians = i * radian_step;

    // Direction of the current vertex from the center of the circle.
    vec2 dir(sin(radians), cos(radians));

    // Build outer-ring vertex.
    (*vertex_p.pos2) = dir * outer_radius + center;
    if (vertex_p.uv)
      (*vertex_p.uv) = 0.5f * (dir + vec2(1.f, 1.f));
    if (vertex_p.pos_offset)
      (*vertex_p.pos_offset) = dir * outer_offset_magnitude;
    if (vertex_p.perim)
      (*vertex_p.perim) = i * outer_vertex_count_reciprocal;
    builder->AddVertexData(vertex, builder->vertex_stride());

    // Build inner-ring vertex.  Only the position and offset may differ from
    // the corresponding outer-ring vertex.
    (*vertex_p.pos2) = dir * inner_radius + center;
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
  FXL_DCHECK(mesh->num_indices() == index_count);
  FXL_DCHECK(
      mesh->bounding_box() ==
      BoundingBox(vec3(center.x - outer_radius, center.y - outer_radius, 0),
                  vec3(center.x + outer_radius, center.y + outer_radius, 0)));
  return mesh;
}

MeshPtr NewSimpleRectangleMesh(MeshBuilderFactory* factory) {
  MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kUV};

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

MeshPtr NewRectangleMesh(MeshBuilderFactory* factory, const MeshSpec& spec,
                         int subdivisions, vec2 size, vec2 top_left,
                         float top_offset_magnitude,
                         float bottom_offset_magnitude) {
  // Compute the number of vertices in the tessellated circle.
  FXL_DCHECK(subdivisions >= 0);
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
  FXL_CHECK(vertex_p.pos2);

  const float vertices_per_side_reciprocal = 1.f / (vertices_per_side - 1);
  for (size_t i = 0; i < vertices_per_side; ++i) {
    // Build bottom vertex.
    (*vertex_p.pos2) =
        top_left + vec2(size.x * i * vertices_per_side_reciprocal, size.y);
    if (vertex_p.uv)
      (*vertex_p.uv) = vec2(i * vertices_per_side_reciprocal, 1.f);
    if (vertex_p.pos_offset)
      (*vertex_p.pos_offset) = vec2(0, 1.f * bottom_offset_magnitude);
    if (vertex_p.perim)
      (*vertex_p.perim) = i * vertices_per_side_reciprocal;
    builder->AddVertexData(vertex, builder->vertex_stride());

    // Build top vertex.
    (*vertex_p.pos2) =
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
  FXL_DCHECK(mesh->num_indices() == index_count);
  return mesh;
}

MeshPtr NewFullScreenMesh(MeshBuilderFactory* factory) {
  MeshSpec spec{MeshAttribute::kPosition2D | MeshAttribute::kUV};

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

MeshPtr NewSphereMesh(MeshBuilderFactory* factory, const MeshSpec& spec,
                      int subdivisions, vec3 center, float radius) {
  FXL_DCHECK(subdivisions >= 0);
  FXL_DCHECK(spec.IsValid());
  size_t vertex_count = 9;
  size_t triangle_count = 8;
  for (int i = 0; i < subdivisions; ++i) {
    // At each level of subdivision, an additional vertex is added for each
    // triangle, and each triangle is split into three.
    vertex_count += triangle_count;
    triangle_count *= 3;
  }

  // Populate initial octahedron.
  auto builder =
      factory->NewMeshBuilder(spec, vertex_count, triangle_count * 3);
  constexpr size_t kMaxVertexSize = 100;
  uint8_t vertex[kMaxVertexSize];
  auto vertex_p =
      GetVertexAttributePointers(vertex, kMaxVertexSize, spec, builder);
  FXL_CHECK(vertex_p.pos3);

  // Positions and UV-coordinates for the initial octahedron.  The vertex with
  // position (-radius, 0, 0) is replicated 4 times, with different UV-coords
  // each time.  This is a consequence of surface parameterization that is
  // described in the header file.
  const vec3 positions[] = {vec3(radius, 0.f, 0.f),  vec3(0.f, 0.f, radius),
                            vec3(0.f, -radius, 0.f), vec3(0.f, 0.f, -radius),
                            vec3(0.f, radius, 0.f),  vec3(-radius, 0.f, 0.f),
                            vec3(-radius, 0.f, 0.f), vec3(-radius, 0.f, 0.f),
                            vec3(-radius, 0.f, 0.f)};
  const vec2 uv_coords[] = {vec2(.5f, .5f), vec2(1.f, .5f), vec2(.5f, 0.f),
                            vec2(0.f, .5f), vec2(.5f, 1.f), vec2(0.f, 0.f),
                            vec2(1.f, 0.f), vec2(1.f, 1.f), vec2(0.f, 1.f)};

  for (int i = 0; i < 9; ++i) {
    (*vertex_p.pos3) = positions[i] + center;
    if (vertex_p.uv) {
      (*vertex_p.uv) = uv_coords[i];
    }
    builder->AddVertexData(vertex, builder->vertex_stride());
  }
  builder->AddTriangle(0, 1, 2)
      .AddTriangle(0, 2, 3)
      .AddTriangle(0, 3, 4)
      .AddTriangle(0, 4, 1)
      .AddTriangle(5, 2, 1)
      .AddTriangle(6, 3, 2)
      .AddTriangle(7, 4, 3)
      .AddTriangle(8, 1, 4);

  // TODO(ES-32): this is a hack to ease implementation.  We don't currently
  // need any tessellated spheres; this is just a way to verify that 3D meshes
  // are working properly.
  FXL_DCHECK(spec.flags == (MeshAttribute::kPosition3D | MeshAttribute::kUV))
      << "Tessellated sphere must have UV-coordinates.";
  size_t position_offset = reinterpret_cast<uint8_t*>(vertex_p.pos3) - vertex;
  size_t uv_offset = reinterpret_cast<uint8_t*>(vertex_p.uv) - vertex;
  while (subdivisions-- > 0) {
    // For each level of subdivision, iterate over all existing triangles and
    // split them into three.
    // TODO(ES-32): see comment in header file... this approach is broken, but
    // sufficient for our current purpose.
    const size_t subdiv_triangle_count = builder->index_count() / 3;
    FXL_DCHECK(subdiv_triangle_count * 3 == builder->index_count());

    for (size_t tri_ind = 0; tri_ind < subdiv_triangle_count; ++tri_ind) {
      // Obtain indices for the current triangle, and the position/UV coords for
      // the corresponding vertices.
      uint32_t* tri = builder->GetIndex(tri_ind * 3);
      uint32_t ind0 = tri[0];
      uint32_t ind1 = tri[1];
      uint32_t ind2 = tri[2];
      uint8_t* vert0 = builder->GetVertex(ind0);
      uint8_t* vert1 = builder->GetVertex(ind1);
      uint8_t* vert2 = builder->GetVertex(ind2);
      vec3 pos0 = *reinterpret_cast<vec3*>(vert0 + position_offset);
      vec3 pos1 = *reinterpret_cast<vec3*>(vert1 + position_offset);
      vec3 pos2 = *reinterpret_cast<vec3*>(vert2 + position_offset);
      vec2 uv0 = *reinterpret_cast<vec2*>(vert0 + uv_offset);
      vec2 uv1 = *reinterpret_cast<vec2*>(vert1 + uv_offset);
      vec2 uv2 = *reinterpret_cast<vec2*>(vert2 + uv_offset);

      // Create a new vertex by averaging the existing vertex attributes.
      (*vertex_p.pos3) =
          center + radius * glm::normalize((pos0 + pos1 + pos2) / 3.f - center);
      (*vertex_p.uv) = (uv0 + uv1 + uv2) / 3.f;
      builder->AddVertexData(vertex, builder->vertex_stride());

      // Replace the current triangle in-place with a new triangle that refers
      // to the new vertex.  Then, add two new triangles that also refer to the
      // new vertex.
      uint32_t new_ind = builder->vertex_count() - 1;
      tri[2] = new_ind;
      builder->AddTriangle(ind1, ind2, new_ind)
          .AddTriangle(ind2, ind0, new_ind);
    }
  }
  return builder->Build();
}

}  // namespace escher
