// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/util/semantic_transform.h"

#include <gtest/gtest.h>

namespace accessibility_test {
namespace {

TEST(SemanticTransformTest, InitialIdentity) {
  a11y::SemanticTransform transform;

  EXPECT_FLOAT_EQ(transform.scale_vector()[0], 1);
  EXPECT_FLOAT_EQ(transform.scale_vector()[1], 1);
  EXPECT_FLOAT_EQ(transform.scale_vector()[2], 1);
  EXPECT_FLOAT_EQ(transform.translation_vector()[0], 0);
  EXPECT_FLOAT_EQ(transform.translation_vector()[1], 0);
  EXPECT_FLOAT_EQ(transform.translation_vector()[2], 0);

  fuchsia::ui::gfx::vec3 init_point = {1.3, 2.4, 3.5};
  auto new_point = transform.Apply(init_point);
  EXPECT_FLOAT_EQ(new_point.x, init_point.x);
  EXPECT_FLOAT_EQ(new_point.y, init_point.y);
  EXPECT_FLOAT_EQ(new_point.z, init_point.z);
}

TEST(SemanticTransformTest, AccumulatedTransforms) {
  fuchsia::ui::gfx::mat4 transform1;
  // Scale factors
  transform1.matrix[0] = 1.2;
  transform1.matrix[5] = 3.4;
  transform1.matrix[10] = 5.6;
  // Translation values
  transform1.matrix[12] = -1.0;
  transform1.matrix[13] = 2.5;
  transform1.matrix[14] = 1.5;
  transform1.matrix[15] = 1.0;

  fuchsia::ui::gfx::mat4 transform2;
  // Scale factors
  transform2.matrix[0] = -1.0;
  transform2.matrix[5] = 2.3;
  transform2.matrix[10] = 7.1;
  // Translation values
  transform2.matrix[12] = 4.3;
  transform2.matrix[13] = 3.14;
  transform2.matrix[14] = -1.27;
  transform2.matrix[15] = 1.0;

  a11y::SemanticTransform transform;
  transform.ChainLocalTransform(transform1);
  transform.ChainLocalTransform(transform2);

  EXPECT_FLOAT_EQ(transform.scale_vector()[0], -1.2);
  EXPECT_FLOAT_EQ(transform.scale_vector()[1], 7.82);
  EXPECT_FLOAT_EQ(transform.scale_vector()[2], 39.76);
  EXPECT_FLOAT_EQ(transform.translation_vector()[0], 5.3);
  EXPECT_FLOAT_EQ(transform.translation_vector()[1], 8.89);
  EXPECT_FLOAT_EQ(transform.translation_vector()[2], 9.38);

  fuchsia::ui::gfx::vec3 init_point = {1.3, 2.4, 3.5};
  auto new_point = transform.Apply(init_point);
  EXPECT_FLOAT_EQ(new_point.x, init_point.x * -1.2 + 5.3);
  EXPECT_FLOAT_EQ(new_point.y, init_point.y * 7.82 + 8.89);
  EXPECT_FLOAT_EQ(new_point.z, init_point.z * 39.76 + 9.38);
}

}  // namespace
}  // namespace accessibility_test
