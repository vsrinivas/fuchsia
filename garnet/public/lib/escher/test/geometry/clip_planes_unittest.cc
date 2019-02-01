// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/geometry/clip_planes.h"

#include "lib/escher/geometry/bounding_box.h"

#include "gtest/gtest.h"

namespace {

using namespace escher;

TEST(ClipPlanes, Validity) {
  ClipPlanes planes;
  auto& p = planes.planes;
  p[0] = p[1] = p[2] = p[3] = p[4] = p[5] = vec4(1, 0, 0, 0);
  EXPECT_TRUE(planes.IsValid());
  p[0] = vec4(1, 0, 0, 100);
  EXPECT_TRUE(planes.IsValid());
  p[0] = vec4(0, 1, 0, 100);
  EXPECT_TRUE(planes.IsValid());
  p[0] = vec4(0, 0, 1, 100);
  EXPECT_TRUE(planes.IsValid());
  p[0] = vec4(1, 0, 0, 100);
  EXPECT_TRUE(planes.IsValid());

  constexpr float kSqrt2 = 0.70710678118;
  p[0] = vec4(kSqrt2, kSqrt2, 0, 100);
  EXPECT_TRUE(planes.IsValid());

  p[0] = vec4(1, 1, 0, 100);
  EXPECT_FALSE(planes.IsValid());
}

TEST(ClipPlanes, ClipIfSmallerThanX) {
  ClipPlanes planes;

  constexpr float X = 40;
  auto& p = planes.planes;
  p[0] = p[1] = p[2] = p[3] = p[4] = p[5] = vec4(1, 0, 0, -X);

  EXPECT_TRUE(planes.ClipsPoint(vec3(X - 1, 0, 0)));
  EXPECT_TRUE(planes.ClipsPoint(vec3(X - 1, 100, 0)));
  EXPECT_TRUE(planes.ClipsPoint(vec3(X - 1, 0, 100)));
  EXPECT_TRUE(planes.ClipsPoint(vec3(X - 1, 100, 100)));

  EXPECT_FALSE(planes.ClipsPoint(vec3(X, 0, 0)));
  EXPECT_FALSE(planes.ClipsPoint(vec3(X, 100, 0)));
  EXPECT_FALSE(planes.ClipsPoint(vec3(X, 0, 100)));
  EXPECT_FALSE(planes.ClipsPoint(vec3(X, 100, 100)));

  EXPECT_FALSE(planes.ClipsPoint(vec3(X + 1, 0, 0)));
  EXPECT_FALSE(planes.ClipsPoint(vec3(X + 1, 100, 0)));
  EXPECT_FALSE(planes.ClipsPoint(vec3(X + 1, 0, 100)));
  EXPECT_FALSE(planes.ClipsPoint(vec3(X + 1, 100, 100)));
}

TEST(ClipPlanes, FromBox) {
  BoundingBox box({10, 100, 1000}, vec3{20, 200, 2000});
  auto planes = ClipPlanes::FromBox(box);

  EXPECT_FALSE(planes.ClipsPoint({10, 100, 1000}));
  EXPECT_FALSE(planes.ClipsPoint({15, 150, 1500}));
  EXPECT_FALSE(planes.ClipsPoint({20, 200, 2000}));

  EXPECT_TRUE(planes.ClipsPoint({9, 100, 1000}));
  EXPECT_TRUE(planes.ClipsPoint({10, 99, 1000}));
  EXPECT_TRUE(planes.ClipsPoint({10, 100, 999}));
  EXPECT_TRUE(planes.ClipsPoint({21, 200, 2000}));
  EXPECT_TRUE(planes.ClipsPoint({20, 201, 2000}));
  EXPECT_TRUE(planes.ClipsPoint({20, 200, 2001}));
}

}  // namespace
