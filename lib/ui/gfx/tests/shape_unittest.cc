// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>

#include "garnet/lib/ui/gfx/resources/shapes/circle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "lib/ui/scenic/cpp/commands.h"

#include "gtest/gtest.h"

namespace scenic {
namespace gfx {
namespace test {
namespace {
using escher::ray4;
using escher::vec2;
using escher::vec3;
using escher::vec4;

constexpr float kSqrt2_2 = M_SQRT2 / 2.f;
constexpr vec4 kDownVector{0.f, 0.f, -1.f, 0.f};
constexpr vec4 kUpVector{0.f, 0.f, 1.f, 0.f};
constexpr vec4 kSideVector{1.f, 0.f, 0.f, 0.f};
constexpr vec4 kZeroVector{0.f, 0.f, 0.f, 0.f};
constexpr vec4 kAngledVector{2.f, -1.f, -.5f, 0.f};
}  // namespace

using ShapeTest = SessionTest;

TEST_F(ShapeTest, Circle) {
  const scenic::ResourceId id = 1;
  EXPECT_TRUE(Apply(scenic::NewCreateCircleCmd(id, 50.f)));

  auto circle = FindResource<CircleShape>(id);
  ASSERT_NE(nullptr, circle.get());
  EXPECT_EQ(50.f, circle->radius());

  EXPECT_TRUE(circle->ContainsPoint(vec2(0.f, 0.f)));
  EXPECT_TRUE(circle->ContainsPoint(vec2(50.f, 0.f)));
  EXPECT_TRUE(circle->ContainsPoint(vec2(0.f, -50.f)));
  EXPECT_TRUE(circle->ContainsPoint(vec2(-50.f, 0.f)));
  EXPECT_TRUE(circle->ContainsPoint(vec2(0.f, 50.f)));
  EXPECT_TRUE(circle->ContainsPoint(vec2(50.f * kSqrt2_2, 50.f * kSqrt2_2)));

  EXPECT_FALSE(circle->ContainsPoint(vec2(50.1f, 0.f)));
  EXPECT_FALSE(circle->ContainsPoint(vec2(0.f, -50.1f)));
  EXPECT_FALSE(circle->ContainsPoint(vec2(-50.1f, 0.f)));
  EXPECT_FALSE(circle->ContainsPoint(vec2(0.f, 50.1f)));
  EXPECT_FALSE(circle->ContainsPoint(vec2(50.1f * kSqrt2_2, 50.1f * kSqrt2_2)));

  float distance = -1.f;
  EXPECT_TRUE(circle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kDownVector}, &distance));
  EXPECT_EQ(0.f, distance);
  EXPECT_TRUE(circle->GetIntersection(
      ray4{vec4(0.f, 0.f, 5.f, 1.f), kDownVector}, &distance));
  EXPECT_EQ(5.f, distance);
  EXPECT_TRUE(circle->GetIntersection(
      ray4{vec4(50.f, 0.f, 10.f, 1.f), kDownVector}, &distance));
  EXPECT_EQ(10.f, distance);
  EXPECT_TRUE(circle->GetIntersection(
      ray4{vec4(50.f * kSqrt2_2, -50.f * kSqrt2_2, 0.f, 1.f) -
               40.f * kAngledVector,
           kAngledVector},
      &distance));
  EXPECT_EQ(40.f, distance);
  EXPECT_TRUE(circle->GetIntersection(
      ray4{vec4(50.f * kSqrt2_2 * 3.f, -50.f * kSqrt2_2 * 3.f, 0.f, 3.f) -
               40.f * kAngledVector,
           kAngledVector},
      &distance));
  EXPECT_EQ(40.f, distance);

  EXPECT_FALSE(circle->GetIntersection(
      ray4{vec4(0.f, 0.f, -1.f, 1.f), kDownVector}, &distance));
  EXPECT_FALSE(circle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kUpVector}, &distance));
  EXPECT_FALSE(circle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kSideVector}, &distance));
  EXPECT_FALSE(circle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kZeroVector}, &distance));
  EXPECT_FALSE(circle->GetIntersection(
      ray4{vec4(50.1f * kSqrt2_2, -50.1f * kSqrt2_2, 0.f, 1.f) -
               40.f * kAngledVector,
           kAngledVector},
      &distance));
}

TEST_F(ShapeTest, Rectangle) {
  const scenic::ResourceId id = 1;
  EXPECT_TRUE(Apply(scenic::NewCreateRectangleCmd(id, 30.f, 40.f)));

  auto rectangle = FindResource<RectangleShape>(id);
  ASSERT_NE(nullptr, rectangle.get());
  EXPECT_EQ(30.f, rectangle->width());
  EXPECT_EQ(40.f, rectangle->height());

  EXPECT_TRUE(rectangle->ContainsPoint(vec2(0.f, 0.f)));
  EXPECT_TRUE(rectangle->ContainsPoint(vec2(15.f, 0.f)));
  EXPECT_TRUE(rectangle->ContainsPoint(vec2(15.f, -20.f)));
  EXPECT_TRUE(rectangle->ContainsPoint(vec2(0.f, -20.f)));
  EXPECT_TRUE(rectangle->ContainsPoint(vec2(-15.f, -20.f)));
  EXPECT_TRUE(rectangle->ContainsPoint(vec2(-15.f, 0.f)));
  EXPECT_TRUE(rectangle->ContainsPoint(vec2(-15.f, 20.f)));
  EXPECT_TRUE(rectangle->ContainsPoint(vec2(0.f, 20.f)));
  EXPECT_TRUE(rectangle->ContainsPoint(vec2(15.f, 20.f)));

  EXPECT_FALSE(rectangle->ContainsPoint(vec2(15.1f, 0.f)));
  EXPECT_FALSE(rectangle->ContainsPoint(vec2(15.1f, -20.1f)));
  EXPECT_FALSE(rectangle->ContainsPoint(vec2(0.f, -20.1f)));
  EXPECT_FALSE(rectangle->ContainsPoint(vec2(-15.1f, -20.1f)));
  EXPECT_FALSE(rectangle->ContainsPoint(vec2(-15.1f, 0.f)));
  EXPECT_FALSE(rectangle->ContainsPoint(vec2(-15.1f, 20.1f)));
  EXPECT_FALSE(rectangle->ContainsPoint(vec2(0.f, 20.1f)));
  EXPECT_FALSE(rectangle->ContainsPoint(vec2(15.1f, 20.1f)));

  float distance = -1.f;
  EXPECT_TRUE(rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kDownVector}, &distance));
  EXPECT_EQ(0.f, distance);
  EXPECT_TRUE(rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, 5.f, 1.f), kDownVector}, &distance));
  EXPECT_EQ(5.f, distance);
  EXPECT_TRUE(rectangle->GetIntersection(
      ray4{vec4(15.f, 20.f, 10.f, 1.f), kDownVector}, &distance));
  EXPECT_EQ(10.f, distance);
  EXPECT_TRUE(rectangle->GetIntersection(
      ray4{vec4(15.f, -20.f, 0.f, 1.f) - 40.f * kAngledVector, kAngledVector},
      &distance));
  EXPECT_EQ(40.f, distance);
  EXPECT_TRUE(rectangle->GetIntersection(
      ray4{vec4(15.f * 3.f, -20.f * 3.f, 0.f, 3.f) - 40.f * kAngledVector,
           kAngledVector},
      &distance));
  EXPECT_EQ(40.f, distance);

  EXPECT_FALSE(rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, -1.f, 1.f), kDownVector}, &distance));
  EXPECT_FALSE(rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kUpVector}, &distance));
  EXPECT_FALSE(rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kSideVector}, &distance));
  EXPECT_FALSE(rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kZeroVector}, &distance));
  EXPECT_FALSE(rectangle->GetIntersection(
      ray4{vec4(15.1f, -20.1f, 0.f, 1.f) - 40.f * kAngledVector, kAngledVector},
      &distance));
}

// TODO(MZ-159): This test needs a rounded rect factory to run but it is
// not currently available in the engine for tests.
TEST_F(ShapeTest, DISABLED_RoundedRectangle) {
  const scenic::ResourceId id = 1;
  EXPECT_TRUE(Apply(scenic::NewCreateRoundedRectangleCmd(id, 30.f, 40.f, 2.f,
                                                         4.f, 6.f, 8.f)));

  auto rounded_rectangle = FindResource<RoundedRectangleShape>(id);
  ASSERT_NE(nullptr, rounded_rectangle.get());
  EXPECT_EQ(30.f, rounded_rectangle->width());
  EXPECT_EQ(40.f, rounded_rectangle->height());

  EXPECT_TRUE(rounded_rectangle->ContainsPoint(vec2(0.f, 0.f)));
  EXPECT_TRUE(rounded_rectangle->ContainsPoint(vec2(15.f, 0.f)));
  EXPECT_TRUE(rounded_rectangle->ContainsPoint(
      vec2(15.f - 4.f * kSqrt2_2, -20.f + 4.f * kSqrt2_2)));
  EXPECT_TRUE(rounded_rectangle->ContainsPoint(vec2(0.f, -20.f)));
  EXPECT_TRUE(rounded_rectangle->ContainsPoint(
      vec2(-15.f + 2.f * kSqrt2_2, -20.f + 2.f * kSqrt2_2)));
  EXPECT_TRUE(rounded_rectangle->ContainsPoint(vec2(-15.f, 0.f)));
  EXPECT_TRUE(rounded_rectangle->ContainsPoint(
      vec2(-15.f + 8.f * kSqrt2_2, 20.f - 8.f * kSqrt2_2)));
  EXPECT_TRUE(rounded_rectangle->ContainsPoint(vec2(0.f, 20.f)));
  EXPECT_TRUE(rounded_rectangle->ContainsPoint(
      vec2(15.f - 6.f * kSqrt2_2, 20.f - 6.f * kSqrt2_2)));

  EXPECT_FALSE(rounded_rectangle->ContainsPoint(vec2(15.1f, 0.f)));
  EXPECT_FALSE(rounded_rectangle->ContainsPoint(vec2(15.f, -20.f)));
  EXPECT_FALSE(rounded_rectangle->ContainsPoint(vec2(0.f, -20.1f)));
  EXPECT_FALSE(rounded_rectangle->ContainsPoint(vec2(-15.f, -20.f)));
  EXPECT_FALSE(rounded_rectangle->ContainsPoint(vec2(-15.1f, 0.f)));
  EXPECT_FALSE(rounded_rectangle->ContainsPoint(vec2(-15.f, 20.f)));
  EXPECT_FALSE(rounded_rectangle->ContainsPoint(vec2(0.f, 20.1f)));
  EXPECT_FALSE(rounded_rectangle->ContainsPoint(vec2(15.f, 20.f)));

  float distance = -1.f;
  EXPECT_TRUE(rounded_rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kDownVector}, &distance));
  EXPECT_EQ(0.f, distance);
  EXPECT_TRUE(rounded_rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, 5.f, 1.f), kDownVector}, &distance));
  EXPECT_EQ(5.f, distance);
  EXPECT_TRUE(rounded_rectangle->GetIntersection(
      ray4{vec4(15.f - 4.f * kSqrt2_2, -20.f + 4.f * kSqrt2_2, 10.f, 1.f),
           kDownVector},
      &distance));
  EXPECT_EQ(10.f, distance);
  EXPECT_TRUE(rounded_rectangle->GetIntersection(
      ray4{vec4(15.f - 4.f * kSqrt2_2, -20.f + 4.f * kSqrt2_2, 0.f, 1.f) -
               40.f * kAngledVector,
           kAngledVector},
      &distance));
  EXPECT_EQ(40.f, distance);
  EXPECT_TRUE(rounded_rectangle->GetIntersection(
      ray4{vec4((15.f - 4.f * kSqrt2_2) * 3.f, (-20.f + 4.f * kSqrt2_2) * 3.f,
                0.f, 3.f) -
               40.f * kAngledVector,
           kAngledVector},
      &distance));
  EXPECT_EQ(40.f, distance);

  EXPECT_FALSE(rounded_rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, -1.f, 1.f), kDownVector}, &distance));
  EXPECT_FALSE(rounded_rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kUpVector}, &distance));
  EXPECT_FALSE(rounded_rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kSideVector}, &distance));
  EXPECT_FALSE(rounded_rectangle->GetIntersection(
      ray4{vec4(0.f, 0.f, 0.f, 1.f), kZeroVector}, &distance));
  EXPECT_FALSE(rounded_rectangle->GetIntersection(
      ray4{vec4(15.1f - 4.f * kSqrt2_2, -20.1f + 4.f * kSqrt2_2, 0.f, 1.f) -
               40.f * kAngledVector,
           kAngledVector},
      &distance));
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
