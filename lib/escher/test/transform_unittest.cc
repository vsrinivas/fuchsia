// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>

#include <glm/glm.hpp>

#include "escher/geometry/transform.h"
#include "gtest/gtest.h"
#include "lib/ftl/logging.h"

namespace {
using namespace escher;

const float k45Degrees = glm::pi<float>() * 0.25f;
const vec3 kZAxis = vec3(0, 0, 1);
const float kSqrt2 = sqrt(2.0);
const float kHalfSqrt2 = kSqrt2 / 2.0;

TEST(Transform, SimpleTranslation) {
  Transform transform;
  transform.translation = vec3(4, 5, 6);
  vec3 input(1, 2, 3);
  vec3 output(static_cast<mat4>(transform) * vec4(input, 1));
  EXPECT_EQ(input + transform.translation, output);
}

TEST(Transform, SimpleScale) {
  Transform transform;
  transform.scale = vec3(4, 5, 6);
  vec3 input(1, 2, 3);
  vec3 output(static_cast<mat4>(transform) * vec4(input, 1));
  EXPECT_EQ(input * transform.scale, output);
}

TEST(Transform, SimpleRotation) {
  Transform transform;
  transform.rotation = glm::angleAxis(k45Degrees, kZAxis);
  vec3 input(1, 0, -5);
  vec3 output(static_cast<mat4>(transform) * vec4(input, 1));
  vec3 expected_output(kHalfSqrt2, kHalfSqrt2, -5);
  EXPECT_NEAR(0.f, glm::distance(expected_output, output), 0.00001f);
}

TEST(Transform, AllTogetherNow) {
  Transform transform;
  transform.translation = vec3(10, 10, 10);
  transform.scale = vec3(2, 2, 2);
  transform.rotation = glm::angleAxis(k45Degrees, kZAxis);
  transform.anchor = vec3(3, 4, -4);

  vec3 input(4, 4, -5);
  vec3 output(static_cast<mat4>(transform) * vec4(input, 1));

  // With respect to the anchor, the input is vec3(1, 0, -1), which is
  // rotated/scaled to vec3(kSqrt2, kSqrt2, -2).  With respect to the origin,
  // the rotated/scaled value is vec3(3 + kSqrt2, 4 + kSqrt2, -6).  Finally,
  // this last value is translated by vec3(10, 10, 10).
  vec3 expected_output(13 + kSqrt2, 14 + kSqrt2, 4);
  EXPECT_NEAR(0.f, glm::distance(expected_output, output), 0.00001f);
}

}  // namespace
