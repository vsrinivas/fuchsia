// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/mesh/indexed_triangle_mesh_clip.h"

#include "gtest/gtest.h"
#include "src/ui/lib/escher/mesh/tessellation.h"

namespace {

using namespace escher;

// Very simple test that is easy to understand.
TEST(IndexedTriangleMeshClip, OneTriangle2d) {
  IndexedTriangleMesh2d<nullptr_t, vec2> mesh{.indices = {0, 1, 2},
                                              .positions = {vec2(0, 0), vec2(1, 0), vec2(0, 3)},
                                              .attributes2 = {vec2(0, 0), vec2(1, 0), vec2(0, 1)}};

  // Clip two vertices, keeping one tip of the original triangle.
  std::vector<plane2> planes = {plane2(vec2(1, 0), 0.5f)};
  auto result = IndexedTriangleMeshClip(mesh, planes);
  auto& output_mesh = result.first;
  EXPECT_EQ(output_mesh.indices.size(), 3U);
  EXPECT_EQ(output_mesh.positions.size(), 3U);
  EXPECT_EQ(output_mesh.attributes2.size(), 3U);
  // The unused attributes should be unpopulated.
  EXPECT_EQ(output_mesh.attributes1.size(), 0U);
  EXPECT_EQ(output_mesh.attributes3.size(), 0U);
  for (size_t i = 0; i < output_mesh.vertex_count(); ++i) {
    auto& pos = output_mesh.positions[i];
    auto& attr = output_mesh.attributes2[i];
    if (pos == vec2(1, 0)) {
      EXPECT_EQ(attr, vec2(1, 0));
    } else if (pos == vec2(0.5f, 0)) {
      EXPECT_EQ(attr, vec2(0.5f, 0));
    } else if (pos == vec2(0.5f, 1.5f)) {
      EXPECT_EQ(attr, vec2(0.5f, 0.5f));
    } else {
      // This should not be reached.
      EXPECT_TRUE(false);
    }
  }

  // Use the same plane (but with the opposite orientation) to clip one tip of
  // the triangle, leaving behind a quad that is split into two triangles.
  planes = {plane2(vec2(-1, 0), -0.5f)};
  result = IndexedTriangleMeshClip(mesh, planes);
  EXPECT_EQ(output_mesh.indices.size(), 6U);
  EXPECT_EQ(output_mesh.positions.size(), 4U);
  EXPECT_EQ(output_mesh.attributes2.size(), 4U);
  for (size_t i = 0; i < output_mesh.vertex_count(); ++i) {
    auto& pos = output_mesh.positions[i];
    auto& attr = output_mesh.attributes2[i];
    if (pos == vec2(0, 0)) {
      EXPECT_EQ(attr, vec2(0, 0));
    } else if (pos == vec2(0, 3)) {
      EXPECT_EQ(attr, vec2(0, 1));
    } else if (pos == vec2(0.5f, 0)) {
      EXPECT_EQ(attr, vec2(0.5f, 0));
    } else if (pos == vec2(0.5f, 1.5f)) {
      EXPECT_EQ(attr, vec2(0.5f, 0.5f));
    } else {
      // This should not be reached.
      EXPECT_TRUE(false);
    }
  }
}

TEST(IndexedTriangleMeshClip, OneTriangle2dManyPlanes) {
  IndexedTriangleMesh2d<nullptr_t, vec2> mesh{.indices = {0, 1, 2},
                                              .positions = {vec2(0, 0), vec2(1, 0), vec2(0, 3)},
                                              .attributes2 = {vec2(0, 0), vec2(1, 0), vec2(0, 1)}};

  // Use many planes to repeatedly clip the triangle in a way that generates a mesh
  // with more vertices than the original mesh.
  std::vector<plane2> planes = {plane2(vec2(-1, 0), -0.5f),  plane2(vec2(-1, 0), -0.49f),
                                plane2(vec2(-1, 0), -0.48f), plane2(vec2(-1, 0), -0.47f),
                                plane2(vec2(-1, 0), -0.46f), plane2(vec2(-1, 0), -0.45f),
                                plane2(vec2(-1, 0), -0.44f), plane2(vec2(-1, 0), -0.43f)};

  auto result = IndexedTriangleMeshClip(mesh, planes);
  auto& output_mesh = result.first;
  EXPECT_EQ(output_mesh.indices.size(), 27U);
  EXPECT_EQ(output_mesh.positions.size(), 11U);
}

// Helper function that returns a list of planes that tightly bounds the
// standard test mesh.
std::vector<plane2> GetStandardTestMeshBoundingPlanes2d() {
  return std::vector<plane2>{{vec2(-2, 0), vec2(1, 0)},
                             {vec2(2, 0), vec2(-1, 0)},
                             {vec2(0, 1), vec2(0, -1)},
                             {vec2(0, -1), vec2(0, 1)},
                             {vec2(-2, 1), glm::normalize(vec2(2, 1))},
                             {vec2(2, 1), glm::normalize(vec2(-2, 1))}};
}
std::vector<plane3> GetStandardTestMeshBoundingPlanes3d() {
  auto planes2d = GetStandardTestMeshBoundingPlanes2d();
  std::vector<plane3> planes3d;
  for (const plane2& p : planes2d) {
    planes3d.push_back(plane3(vec3(p.dir(), 0), p.dist()));
  }
  return planes3d;
}

// Test that planes that are tangent to the perimeter edges of the mesh result
// in a completely unclipped mesh.
template <typename MeshT, typename PlaneT>
void TestUnclippedMesh(const MeshT& mesh, const std::vector<PlaneT>& planes) {
  // First test against individual planes.
  for (auto& p : planes) {
    auto result = IndexedTriangleMeshClip(mesh, std::vector<PlaneT>{p});
    // Since the plane does not clip any vertices, all indices and vertices are
    // left completely unchanged.  Also, the resulting list of clipping planes
    // is empty, indicating that the plane clipped no vertices.
    EXPECT_EQ(mesh.indices, result.first.indices);
    EXPECT_EQ(mesh.positions, result.first.positions);
    EXPECT_EQ(mesh.attributes1, result.first.attributes1);
    EXPECT_TRUE(result.second.empty());
  }

  // Test clipping against all planes at once.
  auto result = IndexedTriangleMeshClip(mesh, planes);
  EXPECT_EQ(mesh.indices, result.first.indices);
  EXPECT_EQ(mesh.positions, result.first.positions);
  EXPECT_EQ(mesh.attributes1, result.first.attributes1);
  EXPECT_TRUE(result.second.empty());
}

TEST(IndexedTriangleMeshClip, Unclipped2d) {
  TestUnclippedMesh(GetStandardTestMesh2d(), GetStandardTestMeshBoundingPlanes2d());
}

TEST(IndexedTriangleMeshClip, Unclipped3d) {
  TestUnclippedMesh(GetStandardTestMesh3d(), GetStandardTestMeshBoundingPlanes3d());
}

// Verify expected behavior when insetting the top and bottom bounding planes
// so that they slightly clip the mesh.
template <typename MeshT, typename PlaneT>
void TestMultipleClips(const MeshT& mesh, const std::vector<PlaneT>& planes) {
  // Take the planes bounding the (screen space) top and bottom of the standard
  // mesh, and inset them slightly so that they clip the mesh.
  PlaneT bottom_plane(planes[2].dir(), planes[2].dist() + 0.1f);
  PlaneT top_plane(planes[3].dir(), planes[3].dist() + 0.1f);

  // Clipping with an inset bottom plane results in two "case 2" clips, and one
  // "case 1" clip.  As a result, we expect one extra triangle and one extra
  // vertex.
  auto bottom_result = IndexedTriangleMeshClip(mesh, std::vector<PlaneT>{bottom_plane});
  EXPECT_EQ(4U, bottom_result.first.triangle_count());
  EXPECT_EQ(6U, bottom_result.first.vertex_count());

  // Clipping with an inset top plane results in two "case 1" clips, and one
  // "case 2" clip.  As a result, we expect two extra triangles and two extra
  // vertices.
  auto top_result = IndexedTriangleMeshClip(mesh, std::vector<PlaneT>{top_plane});
  EXPECT_EQ(5U, top_result.first.triangle_count());
  EXPECT_EQ(7U, top_result.first.vertex_count());

  // Interestingly, clipping with the same two planes in opposite orders gives
  // different results.  This is because clipping by the top_plane first results
  // in two "case 1" diagonal edges being added, which are then clipped by the
  // bottom_plane.  When clipping by the bottom plane first, only one "case 1"
  // diagonal edge is added to later be clipped by the top plane.
  auto bottom_top_result =
      IndexedTriangleMeshClip(mesh, std::vector<PlaneT>{bottom_plane, top_plane});
  auto top_bottom_result =
      IndexedTriangleMeshClip(mesh, std::vector<PlaneT>{top_plane, bottom_plane});
  EXPECT_EQ(7U, bottom_top_result.first.triangle_count());
  EXPECT_EQ(9U, bottom_top_result.first.vertex_count());
  EXPECT_EQ(8U, top_bottom_result.first.triangle_count());
  EXPECT_EQ(10U, top_bottom_result.first.vertex_count());

  // Verify that adding a non-clipping plane in the first/middle/last position
  // doesn't affect the result.
  PlaneT non_clipping_plane = planes[4];
  auto non_clipping_result_1 = IndexedTriangleMeshClip(
      mesh, std::vector<PlaneT>{non_clipping_plane, bottom_plane, top_plane});
  auto non_clipping_result_2 = IndexedTriangleMeshClip(
      mesh, std::vector<PlaneT>{bottom_plane, non_clipping_plane, top_plane});
  auto non_clipping_result_3 = IndexedTriangleMeshClip(
      mesh, std::vector<PlaneT>{bottom_plane, top_plane, non_clipping_plane});
  EXPECT_EQ(bottom_top_result, non_clipping_result_1);
  EXPECT_EQ(bottom_top_result, non_clipping_result_2);
  EXPECT_EQ(bottom_top_result, non_clipping_result_3);
}

TEST(IndexedTriangleMeshClip, MultipleClips2d) {
  TestMultipleClips(GetStandardTestMesh2d(), GetStandardTestMeshBoundingPlanes2d());
}

TEST(IndexedTriangleMeshClip, MultipleClips3d) {
  TestMultipleClips(GetStandardTestMesh3d(), GetStandardTestMeshBoundingPlanes3d());
}

// Check to see that a flat rectangle is made correctly.
TEST(FlatRectangleMeshClip, FlatRectangleTest) {
  const auto& mesh = NewFlatRectangleMesh(vec2(0.5), vec2(0.25), vec2(0, 0), vec2(1, 1));

  // Check that there are the right number of indices, verts and triangles.
  EXPECT_EQ(mesh.index_count(), 6U);
  EXPECT_EQ(mesh.vertex_count(), 4U);
  EXPECT_EQ(mesh.triangle_count(), 2U);

  // Make sure index values are correct.
  EXPECT_EQ(mesh.indices[0], 0U);
  EXPECT_EQ(mesh.indices[1], 1U);
  EXPECT_EQ(mesh.indices[2], 2U);
  EXPECT_EQ(mesh.indices[3], 0U);
  EXPECT_EQ(mesh.indices[4], 2U);
  EXPECT_EQ(mesh.indices[5], 3U);

  // Make sure UV values are correct.
  EXPECT_EQ(mesh.attributes1[0], vec2(0, 1));
  EXPECT_EQ(mesh.attributes1[1], vec2(1, 1));
  EXPECT_EQ(mesh.attributes1[2], vec2(1, 0));
  EXPECT_EQ(mesh.attributes1[3], vec2(0, 0));

  // Make sure position values are correct.
  EXPECT_EQ(mesh.positions[0], vec2(0.5, 0.75));
  EXPECT_EQ(mesh.positions[1], vec2(0.75, 0.75));
  EXPECT_EQ(mesh.positions[2], vec2(0.75, 0.5));
  EXPECT_EQ(mesh.positions[3], vec2(0.5, 0.5));
}

}  // namespace
