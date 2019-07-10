// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/geometry/intersection.h"

#include "gtest/gtest.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/geometry/types.h"

namespace {

using namespace escher;

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

}  // namespace
