// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/math/rotations.h"

#include <gtest/gtest.h>

namespace {

using namespace escher;

void VerifyRotationBetweenVectors(const glm::vec3& from, const glm::vec3& to) {
  // Generate two representations of a rotation that will transform |from| to
  // be parallel to |to|.
  glm::mat4 matrix;
  glm::quat quaternion;
  RotationBetweenVectors(from, to, &matrix);
  RotationBetweenVectors(from, to, &quaternion);

  glm::vec3 normalized_to = glm::normalize(to);
  glm::vec3 transformed_from1 = quaternion * from;
  glm::vec3 transformed_from2 = glm::vec3(matrix * glm::vec4(from, 1.f));

  constexpr float kCloseEnough = 0.000001f;
  glm::vec3 diff = normalized_to - glm::normalize(transformed_from1);
  EXPECT_LT(glm::length(diff), kCloseEnough);
  diff = normalized_to - glm::normalize(transformed_from2);
  EXPECT_LT(glm::length(diff), kCloseEnough);
}

TEST(Rotations, BetweenParallelVectors) {
  glm::vec3 v1(1.f, -99.f, 10.f);
  glm::vec3 v2 = v1;
  VerifyRotationBetweenVectors(v1, v2);

  v2 = v1 * 0.43f;
  VerifyRotationBetweenVectors(v1, v2);

  v2 = v1 * -0.43f;
  VerifyRotationBetweenVectors(v1, v2);

  v2 = v1 * 1176.43f;
  VerifyRotationBetweenVectors(v1, v2);

  v2 = v1 * -1176.43f;
  VerifyRotationBetweenVectors(v1, v2);
}

TEST(Rotations, BetweenPerpendicularVectors) {
  glm::vec3 v1(3.f, 0.f, 0.f);
  glm::vec3 v2(0.f, 5.f, 0.f);
  glm::vec3 v3(0.f, 0.f, 7.f);
  VerifyRotationBetweenVectors(v1, v2);
  VerifyRotationBetweenVectors(v2, v1);
  VerifyRotationBetweenVectors(v2, v3);
  VerifyRotationBetweenVectors(v3, v2);
  VerifyRotationBetweenVectors(v3, v1);
  VerifyRotationBetweenVectors(v1, v3);

  // Vectors will remain perpendicular after an arbitrary rotation.
  glm::quat rotation;
  glm::vec3 arbitrary(11.f, 19.f, 23.f);
  RotationBetweenVectors(glm::vec3(0.f, 0.f, 1.f), arbitrary, &rotation);
  v1 = rotation * v1;
  v2 = rotation * v2;
  v3 = rotation * v3;
  VerifyRotationBetweenVectors(v1, v2);
  VerifyRotationBetweenVectors(v2, v1);
  VerifyRotationBetweenVectors(v2, v3);
  VerifyRotationBetweenVectors(v3, v2);
  VerifyRotationBetweenVectors(v3, v1);
  VerifyRotationBetweenVectors(v1, v3);
}

}  // namespace
