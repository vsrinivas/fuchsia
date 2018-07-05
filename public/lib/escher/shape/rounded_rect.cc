// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/shape/rounded_rect.h"

#include "lib/escher/geometry/types.h"
#include "lib/escher/shape/mesh_spec.h"
#include "lib/fxl/logging.h"

namespace escher {

namespace {

// Number of times that the quarter-circle that makes up each corner is
// sub-divided.  For example, if this is zero, then the corner consists of
// a single right-angled triangle.
constexpr uint32_t kCornerDivisions = 8;

// The central "square" consists of 4 vertices connected to their neighbors, and
// to a central vertex.  The 4 arms of the "cross" each share 2 vertices with
// the central square, and add 2 more.  Each corner adds kCornerDivisions more.
constexpr uint32_t kVertexCount = 1 + 4 + (4 * 2) + 4 * kCornerDivisions;

// The central "square" consists of 4 triangles, and the 4 arms of the "cross"
// each have 2.
constexpr uint32_t kTriangleCount = 4 + (4 * 2) + 4 * (kCornerDivisions + 1);

// Triangles have 3 indices.
constexpr uint32_t kIndexCount = kTriangleCount * 3;

// Vertex format used when tessellating a RoundedRectSpec (for now, only a
// single format is supported).
struct PosUvVertex {
  vec2 pos;
  vec2 uv;
};

}  // anonymous namespace

RoundedRectSpec::RoundedRectSpec(float width, float height,
                                 float top_left_radius, float top_right_radius,
                                 float bottom_right_radius,
                                 float bottom_left_radius)
    : width(width),
      height(height),
      top_left_radius(top_left_radius),
      top_right_radius(top_right_radius),
      bottom_right_radius(bottom_right_radius),
      bottom_left_radius(bottom_left_radius) {}

// Return the number of vertices and indices that are required to tessellate the
// specified rounded-rect.
std::pair<uint32_t, uint32_t> GetRoundedRectMeshVertexAndIndexCounts(
    const RoundedRectSpec& spec) {
  return std::make_pair(kVertexCount, kIndexCount);
}

// See escher/shape/doc/RoundedRectTessellation.JPG.
void GenerateRoundedRectIndices(const RoundedRectSpec& spec,
                                const MeshSpec& mesh_spec, void* indices_out,
                                uint32_t max_bytes) {
  FXL_DCHECK(max_bytes == kIndexCount * sizeof(uint32_t));
  uint32_t* indices = static_cast<uint32_t*>(indices_out);

  // Central square triangles.
  indices[0] = 0;
  indices[1] = 4;
  indices[2] = 1;
  indices[3] = 0;
  indices[4] = 1;
  indices[5] = 2;
  indices[6] = 0;
  indices[7] = 2;
  indices[8] = 3;
  indices[9] = 0;
  indices[10] = 3;
  indices[11] = 4;

  // "Cross arm 1"  triangles.
  indices[12] = 1;
  indices[13] = 7;
  indices[14] = 2;
  indices[15] = 1;
  indices[16] = 6;
  indices[17] = 7;

  // "Cross arm 2"  triangles.
  indices[18] = 2;
  indices[19] = 9;
  indices[20] = 3;
  indices[21] = 2;
  indices[22] = 8;
  indices[23] = 9;

  // "Cross arm 3"  triangles.
  indices[24] = 3;
  indices[25] = 11;
  indices[26] = 4;
  indices[27] = 3;
  indices[28] = 10;
  indices[29] = 11;

  // "Cross arm 4"  triangles.
  indices[30] = 4;
  indices[31] = 5;
  indices[32] = 1;
  indices[33] = 4;
  indices[34] = 12;
  indices[35] = 5;

  // WARNING: here's where it gets confusing; the number of indices generated is
  // dependent on kCornerDivisions.

  // We've already generated output indices for the "cross triangles".
  constexpr uint32_t kCrossTriangles = 12;
  // Holds the position of the next index to output.
  uint32_t out = kCrossTriangles * 3;
  // Holds the highest index of any vertex used thus far (the central "cross"
  // consists of 13 vertices, whose indices are 0-12).
  uint32_t highest_index = 12;

  // These are the indices of the 4 triangles that would be output if
  // kCornerDivisions were zero.
  const uint32_t corner_tris[] = {1, 6, 5, 2, 8, 7, 3, 10, 9, 4, 12, 11};

  // For each corner, generate wedges in clockwise order.
  for (uint32_t corner = 0; corner < 4; ++corner) {
    // Index of the vertex at the center of the current corner.
    const uint32_t center = corner_tris[corner * 3];
    // As we move clockwise around the corner, this holds the index of the
    // previous perimeter vertex.
    uint32_t prev = corner_tris[corner * 3 + 2];

    for (uint32_t i = 0; i < kCornerDivisions; ++i) {
      indices[out++] = center;
      indices[out++] = prev;
      indices[out++] = prev = ++highest_index;
    }
    // One last triangle (or the only one, if kCornerDivisions == 0).
    indices[out++] = center;
    indices[out++] = prev;
    indices[out++] = corner_tris[corner * 3 + 1];
  }

  FXL_DCHECK(out == kIndexCount);
}

void GenerateRoundedRectVertices(const RoundedRectSpec& spec,
                                 const MeshSpec& mesh_spec, void* vertices_out,
                                 uint32_t max_bytes) {
  const float width = spec.width;
  const float height = spec.height;
  FXL_DCHECK(width >= spec.top_left_radius + spec.top_right_radius);
  FXL_DCHECK(width >= spec.bottom_left_radius + spec.bottom_right_radius);
  FXL_DCHECK(height >= spec.top_left_radius + spec.bottom_left_radius);
  FXL_DCHECK(height >= spec.top_right_radius + spec.bottom_right_radius);
  FXL_DCHECK(max_bytes == kVertexCount * mesh_spec.GetStride());
  FXL_DCHECK(mesh_spec.flags ==
             (MeshAttribute::kPosition2D | MeshAttribute::kUV));
  FXL_DCHECK(0U == mesh_spec.GetAttributeOffset(MeshAttribute::kPosition2D));
  FXL_DCHECK(sizeof(vec2) == mesh_spec.GetAttributeOffset(MeshAttribute::kUV));
  FXL_DCHECK(sizeof(PosUvVertex) == mesh_spec.GetStride());

  // NOTE: for clarity, we first generate the UV-coordinates for each vertex,
  // then make a second pass where we use these UV-coords to generate the vertex
  // positions.

  // Output vertices are writen here.
  PosUvVertex* const verts = static_cast<PosUvVertex*>(vertices_out);

  // First compute UV coordinates of the four "corner centers".
  verts[1].uv =
      vec2(spec.top_left_radius / width, spec.top_left_radius / height);
  verts[2].uv =
      vec2(1.f - spec.top_right_radius / width, spec.top_right_radius / height);
  verts[3].uv = vec2(1.f - spec.bottom_right_radius / width,
                     1.f - spec.bottom_right_radius / height);
  verts[4].uv = vec2(spec.bottom_left_radius / width,
                     1.f - spec.bottom_left_radius / height);

  // The "center" vertex is the average of the four "corner centers".
  verts[0].uv =
      0.25f * ((verts[1].uv + verts[2].uv + verts[3].uv + verts[4].uv));

  // Next, compute UV coords for the 8 vertices where the rounded corners meet
  // the straight side sections.
  verts[6].uv = vec2(verts[1].uv.x, 0.f);
  verts[7].uv = vec2(verts[2].uv.x, 0.f);
  verts[8].uv = vec2(1.f, verts[2].uv.y);
  verts[9].uv = vec2(1.f, verts[3].uv.y);
  verts[10].uv = vec2(verts[3].uv.x, 1.f);
  verts[11].uv = vec2(verts[4].uv.x, 1.f);
  verts[12].uv = vec2(0.f, verts[4].uv.y);
  verts[5].uv = vec2(0.f, verts[1].uv.y);

  // Next, compute UV coords for the vertices that make up the rounded corners.
  // We start at index 13; indices 0-12 were computed above.
  uint32_t out = 13;

  constexpr float kPI = 3.14159265f;
  constexpr float kAngleStep = kPI / 2 / (kCornerDivisions + 1);

  // Generate UV coordinates for top-left corner.
  float angle = kPI + kAngleStep;
  vec2 scale =
      vec2(spec.top_left_radius / width, spec.top_left_radius / height);
  for (size_t i = 0; i < kCornerDivisions; ++i) {
    verts[out++].uv = verts[1].uv + vec2(cos(angle), sin(angle)) * scale;
    angle += kAngleStep;
  }

  // Generate UV coordinates for top-right corner.
  angle = 1.5f * kPI + kAngleStep;
  scale = vec2(spec.top_right_radius / width, spec.top_right_radius / height);
  for (size_t i = 0; i < kCornerDivisions; ++i) {
    verts[out++].uv = verts[2].uv + vec2(cos(angle), sin(angle)) * scale;
    angle += kAngleStep;
  }

  // Generate UV coordinates for bottom-right corner.
  angle = kAngleStep;
  scale =
      vec2(spec.bottom_right_radius / width, spec.bottom_right_radius / height);
  for (size_t i = 0; i < kCornerDivisions; ++i) {
    verts[out++].uv = verts[3].uv + vec2(cos(angle), sin(angle)) * scale;
    angle += kAngleStep;
  }

  // Generate UV coordinates for bottom-right corner.
  angle = 0.5f * kPI + kAngleStep;
  scale =
      vec2(spec.bottom_left_radius / width, spec.bottom_left_radius / height);
  for (size_t i = 0; i < kCornerDivisions; ++i) {
    verts[out++].uv = verts[4].uv + vec2(cos(angle), sin(angle)) * scale;
    angle += kAngleStep;
  }

  // The hard part is finished!  Make one final pass to generate the vertex
  // positions from the UV-coordinates.
  FXL_DCHECK(out == kVertexCount);
  const vec2 extent(width, height);
  const vec2 offset = -0.5f * extent;
  for (size_t i = 0; i < kVertexCount; ++i) {
    verts[i].pos = verts[i].uv * extent + offset;
  }
}

bool RoundedRectSpec::ContainsPoint(vec2 point) const {
  // Adjust point so that we can test against a rect with bounds (0,0),(w,h).
  // This is saves some multiplications, but mostly makes the code below more
  // concise.
  point += vec2(0.5f * width, 0.5f * height);

  // Check if point is outside of the bounding rectangle; if so, we don't need
  // to test the corner cases.
  if (point.x < 0 || point.y < 0 || point.x > width || point.y > height) {
    return false;
  }
  // Now we know that the point is would be contained, if all corner radii were
  // zero.  But, since the radii are not zero, it is possible that the point is
  // outside one of the four quarter-circles that comprise the rounded corners.

  // Check if point is inside the top-left corner.
  {
    // Start by computing the vector from the corner-center to the test-point,
    // and checking whether the direction of the vector is within the corner's
    // quarter-circle arc.  If so, we can immediately determine whether or not
    // the point is contained; otherwise, we need to test against the other
    // corners.
    float rad = top_left_radius;
    vec2 diff = point - vec2(rad, rad);
    if (diff.x < 0.f && diff.y < 0.f) {
      // The direction is within the corner's arc.  Compare squared-distances to
      // determine whether the point is beyond the arc.
      return diff.x * diff.x + diff.y * diff.y <= rad * rad;
    } else {
      // The direction is not within the corner's quarter circle, so we proceed
      // on to the other corners.
    }
  }

  // Check if point is inside the top-right corner.
  //
  // For this corner, and subsequent ones, we follow the same approach as above,
  // with slight adjustments to the computation of the difference-vector, and
  // how we test the direction of that vector.
  {
    float rad = top_right_radius;
    vec2 diff = point - vec2(width - rad, rad);
    if (diff.x > 0.f && diff.y < 0.f) {
      return diff.x * diff.x + diff.y * diff.y <= rad * rad;
    }
  }

  // Check if point is inside the bottom-right corner.
  {
    float rad = bottom_right_radius;
    vec2 diff = point - vec2(width - rad, height - rad);
    if (diff.x > 0.f && diff.y > 0.f) {
      return diff.x * diff.x + diff.y * diff.y <= rad * rad;
    }
  }

  // Check if point is inside the bottom-left corner.
  {
    float rad = bottom_left_radius;
    vec2 diff = point - vec2(rad, height - rad);
    if (diff.x < 0.f && diff.y > 0.f) {
      return diff.x * diff.x + diff.y * diff.y <= rad * rad;
    }
  }

  // Point isn't in the corner areas, so it is definitely contained.
  return true;
}

}  // namespace escher
