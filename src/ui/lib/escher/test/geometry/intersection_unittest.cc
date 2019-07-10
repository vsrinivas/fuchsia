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

}  // namespace
