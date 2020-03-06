// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/geometry/intersection.h"

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/acceleration/uniform_grid.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/mesh/indexed_triangle_mesh_upload.h"
#include "src/ui/lib/escher/mesh/tessellation.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/test/vk/vulkan_tester.h"

namespace {

using namespace escher;

// A brute force ray-mesh intersection function which simply loops over all of the triangles and
// intersects against each of them to find the nearest hit (if any). This is to compare against the
// hit results from the uniform grid, which should match this output exactly.
bool BruteForceRayMeshIntersection(const ray4& ray, const std::vector<vec3> vertices,
                                   const std::vector<uint32_t> indices, float* out_distance) {
  *out_distance = FLT_MAX;

  for (uint32_t i = 0; i < indices.size(); i += 3) {
    const vec3& v0 = vertices[indices[i]];
    const vec3& v1 = vertices[indices[i + 1]];
    const vec3& v2 = vertices[indices[i + 2]];

    float distance;
    if (IntersectRayTriangle(ray, v0, v1, v2, &distance)) {
      if (distance < *out_distance) {
        *out_distance = distance;
      }
    }
  }

  return *out_distance < FLT_MAX;
}

TEST(Intersection, SimpleBoundingBox) {
  BoundingBox box(glm::vec3(0, 0, 0), glm::vec3(5, 5, 5));
  ray4 ray{.origin = glm::vec4(1, 1, -1, 1), .direction = glm::vec4(0, 0, 1, 0)};

  Interval out_interval;
  bool result = IntersectRayBox(ray, box, &out_interval);
  EXPECT_TRUE(result);
}

TEST(Intersection, BoundingBoxBehind) {
  BoundingBox box(glm::vec3(0, 0, 0), glm::vec3(5, 5, 5));
  ray4 ray{.origin = glm::vec4(1, 1, 10, 1), .direction = glm::vec4(0, 0, 1, 0)};

  Interval out_interval;
  bool result = IntersectRayBox(ray, box, &out_interval);
  EXPECT_TRUE(!result);
}

TEST(Intersection, RayInsideBox) {
  BoundingBox box(glm::vec3(0, 0, 0), glm::vec3(5, 5, 5));
  ray4 ray{.origin = glm::vec4(1, 1, 2, 1), .direction = glm::vec4(0, 0, 1, 0)};

  Interval out_interval;
  bool result = IntersectRayBox(ray, box, &out_interval);
  EXPECT_TRUE(result);
  EXPECT_EQ(out_interval.min(), 0.f);
}

// Test that intersection code still works with nullptrs passed into the
// out min/max fields.
TEST(Intersection, NullInterval) {
  BoundingBox box(glm::vec3(0, 0, 0), glm::vec3(5, 5, 5));
  ray4 ray{.origin = glm::vec4(1, 1, -1, 1), .direction = glm::vec4(0, 0, 1, 0)};

  bool result = IntersectRayBox(ray, box, nullptr);
  EXPECT_TRUE(result);
}

TEST(Intersection, TriangleParallel) {
  ray4 ray{.origin = glm::vec4(0, 0, 0, 1), .direction = glm::vec4(0, 0, 1, 0)};

  // Triangle laying on the YZ plane
  glm::vec3 v0(0, 0, 5);
  glm::vec3 v1(0, 0, 10);
  glm::vec3 v2(0, 5, 7);

  float out_distance;
  bool hit = IntersectRayTriangle(ray, v0, v1, v2, &out_distance);
  EXPECT_FALSE(hit);
}

TEST(Intersection, TriangleBehind) {
  ray4 ray{.origin = glm::vec4(0, 0, 0, 1), .direction = glm::vec4(0, 0, 1, 0)};

  glm::vec3 v0(-5, 0, -5);
  glm::vec3 v1(5, 0, -5);
  glm::vec3 v2(0, 5, -5);

  float out_distance;
  bool hit = IntersectRayTriangle(ray, v0, v1, v2, &out_distance);
  EXPECT_FALSE(hit);
}

TEST(Intersection, TriangleStraightAhead) {
  ray4 ray{.origin = glm::vec4(0, 0, 0, 1), .direction = glm::vec4(0, 0, 1, 0)};

  glm::vec3 v0(-5, 0, 5);
  glm::vec3 v1(5, 0, 5);
  glm::vec3 v2(0, 5, 5);

  float out_distance;
  bool hit = IntersectRayTriangle(ray, v0, v1, v2, &out_distance);
  EXPECT_TRUE(hit);
  EXPECT_EQ(out_distance, 5.f);
}

// Ray is pointed straight up the Y-axis, offset 5 units from the origin.
// Triangle is parallel to the XZ axis at a Y elevation of 100, centered
// over the ray.
TEST(Intersection, TriangleStraightAheadPart2) {
  ray4 ray{.origin = glm::vec4(0, 5, 0, 1), .direction = glm::vec4(0, 1, 0, 0)};

  glm::vec3 v0(-5, 100, 0);
  glm::vec3 v1(5, 100, -5);
  glm::vec3 v2(5, 100, 5);

  float out_distance;
  bool hit = IntersectRayTriangle(ray, v0, v1, v2, &out_distance);
  EXPECT_TRUE(hit);
  EXPECT_EQ(out_distance, 95.f);
}

// The triangle is in front of the ray, but its off to the side and thus
// the ray misses it.
TEST(Intersection, TriangleOffToTheSide) {
  ray4 ray{.origin = glm::vec4(0, 5, 0, 1), .direction = glm::vec4(0, 1, 0, 0)};

  glm::vec3 v0(-15, 100, 0);
  glm::vec3 v1(-5, 100, -5);
  glm::vec3 v2(-5, 100, 5);

  float out_distance;
  bool hit = IntersectRayTriangle(ray, v0, v1, v2, &out_distance);
  EXPECT_FALSE(hit);
}

TEST(Intersection, UniformGridBasicMesh) {
  IndexedTriangleMesh3d<vec2> standard_mesh = GetStandardTestMesh3d();
  MeshSpec mesh_spec{{MeshAttribute::kPosition3D, MeshAttribute::kUV}};
  standard_mesh.bounding_box = BoundingBox(vec3(-2, -1, 10), vec3(2, 1, 12));

  std::unique_ptr<UniformGrid> uniform_grid = UniformGrid::New(standard_mesh);
  uint32_t resolution = uniform_grid->resolution();
  EXPECT_TRUE(uniform_grid);
  EXPECT_EQ(resolution, 1U) << resolution;

  // First ray is pointed straight down the center of the mesh and should hit.
  ray4 ray{.origin = glm::vec4(0, 0, 0, 1), .direction = glm::vec4(0, 0, 1, 0)};
  float out_distance, brute_out_distance;
  bool hit = uniform_grid->Intersect(ray, &out_distance);
  EXPECT_TRUE(hit) << hit;
  EXPECT_EQ(out_distance, 11) << out_distance;

  // Compare the results with that of the brute intersection algorithm.
  bool brute_hit = BruteForceRayMeshIntersection(ray, standard_mesh.positions,
                                                 standard_mesh.indices, &brute_out_distance);
  EXPECT_EQ(hit, brute_hit);
  EXPECT_EQ(out_distance, brute_out_distance);

  // Second ray faces away from the mesh, so it should miss.
  ray4 ray2{.origin = glm::vec4(0, 0, 0, 1), .direction = glm::vec4(0, 0, -1, 0)};
  hit = uniform_grid->Intersect(ray2, &out_distance);
  EXPECT_FALSE(hit) << hit;

  // Third ray faces the mesh but is far off to the side and misses.
  ray4 ray3{.origin = glm::vec4(10, 0, 0, 1), .direction = glm::vec4(0, 0, -1, 0)};
  hit = uniform_grid->Intersect(ray3, &out_distance);
  EXPECT_FALSE(hit) << hit;
}

TEST(Intersection, UniformGridBoxMeshTest) {
  MeshSpec mesh_spec{{MeshAttribute::kPosition3D, MeshAttribute::kUV}};
  IndexedTriangleMesh3d<vec2> cube_mesh = NewCubeIndexedTriangleMesh(mesh_spec);
  cube_mesh.bounding_box = BoundingBox(vec3(0, 0, 0), vec3(1, 1, 1));

  std::unique_ptr<UniformGrid> uniform_grid = UniformGrid::New(cube_mesh);
  uint32_t resolution = uniform_grid->resolution();
  EXPECT_TRUE(uniform_grid);

  for (float x = -1; x == 2; x += 0.2) {
    for (float y = -1; y == 2; y += 0.2) {
      for (float z = -10; z == -5; z += 1) {
        ray4 ray{.origin = vec4(x, y, z, 1), .direction = vec4(0, 0, 1, 0)};
        float out_distance, brute_out_distance;
        bool hit = uniform_grid->Intersect(ray, &out_distance);
        bool brute_hit = BruteForceRayMeshIntersection(ray, cube_mesh.positions, cube_mesh.indices,
                                                       &brute_out_distance);
        EXPECT_EQ(hit, brute_hit);
        if (x > 0 && x < 1 && y > 0 && y < 1) {
          EXPECT_TRUE(hit);
          EXPECT_TRUE(brute_hit);
        } else {
          EXPECT_FALSE(hit);
          EXPECT_FALSE(brute_hit);
        }
        if (hit && brute_hit) {
          EXPECT_EQ(out_distance, brute_out_distance);
        }
      }
    }
  }
}

}  // namespace
