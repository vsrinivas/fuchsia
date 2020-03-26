// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/geometry/transform.h"

#include <cmath>

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

#include <glm/glm.hpp>

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
  transform.translation = vec3(11, 12, 13);
  transform.scale = vec3(1.1f, 1.2f, 1.3f);
  transform.rotation = glm::angleAxis(.75f, glm::normalize(vec3(1, 2, 3)));
  transform.anchor = vec3(1.4f, 1.5f, 1.6f);

  vec3 input(2, 4, 6);
  vec3 output(static_cast<mat4>(transform) * vec4(input, 1));

  vec3 expected_output = input;

  // With respect to the anchor, the input is:
  expected_output -= transform.anchor;

  // This is then axis-scaled:
  expected_output[0] *= transform.scale[0];
  expected_output[1] *= transform.scale[1];
  expected_output[2] *= transform.scale[2];

  // and rotated:
  expected_output = glm::rotate(transform.rotation, expected_output);

  // With respect to the origin, this is:
  expected_output += transform.anchor;

  // Finally, translate:
  expected_output += transform.translation;

  EXPECT_NEAR(0.f, glm::distance(expected_output, output), kEpsilon);
}

}  // namespace
