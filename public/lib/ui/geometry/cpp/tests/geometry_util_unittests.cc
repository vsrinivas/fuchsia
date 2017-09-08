// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/geometry/cpp/geometry_util.h"

#include <array>

#include "gtest/gtest.h"

namespace mozart {
namespace {

TransformPtr CreateTransformFromData(const std::array<float, 16>& data) {
  TransformPtr transform = Transform::New();
  transform->matrix = fidl::Array<float>::New(16);

  memcpy(transform->matrix.data(), &data.front(), 16 * sizeof(float));
  return transform;
}

TransformPtr CreateTestTransform() {
  return CreateTransformFromData(
      {{0.34, 123.7, 89.22, 65.17, 871.12, 87.34, -0.3, -887, 76.2, 2.22222332,
        11.00992, -19, 42, 42, 42, 42}});
}

void ExpectTransformsAreFloatEq(const TransformPtr& lhs,
                                const TransformPtr& rhs) {
  for (size_t row = 0; row < 4; row++) {
    for (size_t col = 0; col < 4; col++) {
      size_t idx = row * 4 + col;
      EXPECT_FLOAT_EQ(lhs->matrix[idx], rhs->matrix[idx]) << "row=" << row
                                                          << ", col=" << col;
    }
  }
}

TEST(RectTest, Comparisons) {
  Rect r1;
  r1.x = 0;
  r1.y = 1;
  r1.width = 2;
  r1.height = 3;

  EXPECT_EQ(r1, r1);

  Rect r2 = r1;
  r2.x = 4;

  EXPECT_NE(r1, r2);

  r2 = r1;
  r2.y = 5;

  EXPECT_NE(r1, r2);

  r2 = r1;
  r2.width = 6;

  EXPECT_NE(r1, r2);

  r2 = r1;
  r2.height = 7;

  EXPECT_NE(r1, r2);
}

TEST(SizeTest, Comparisons) {
  Size s1;
  s1.width = 0;
  s1.height = 1;

  EXPECT_EQ(s1, s1);

  Size s2 = s1;
  s2.width = 2;

  EXPECT_NE(s1, s2);

  s2 = s1;
  s2.height = 3;

  EXPECT_NE(s1, s2);
}

TEST(PointTest, Comparisons) {
  Point p1;
  p1.x = 0;
  p1.y = 1;

  EXPECT_EQ(p1, p1);

  Point p2 = p1;
  p2.x = 2;

  EXPECT_NE(p1, p2);

  p2 = p1;
  p2.y = 3;

  EXPECT_NE(p1, p2);
}

TEST(TransformFunctionsTest, SetIdentityTransform) {
  TransformPtr identity = CreateTransformFromData(
      {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}});
  TransformPtr transform = CreateTestTransform();

  SetIdentityTransform(transform.get());
  ExpectTransformsAreFloatEq(identity, transform);
}

TEST(TransformFunctionsTest, SetTranslationTransform) {
  const float x = 0.5;
  const float y = 10.2;
  const float z = -1.5;

  TransformPtr translated = CreateTransformFromData(
      {{1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1}});
  TransformPtr transform = CreateTestTransform();

  SetTranslationTransform(transform.get(), x, y, z);
  ExpectTransformsAreFloatEq(translated, transform);
}

TEST(TransformFunctionsTest, Translate) {
  const float x = 10.2;
  const float y = 0.5;
  const float z = -4.5;

  TransformPtr transform = CreateTestTransform();
  TransformPtr transform_pristine = transform->Clone();
  TransformPtr transformed = transform->Clone();

  transform_pristine->matrix[0 * 4 + 3] += x;
  transform_pristine->matrix[1 * 4 + 3] += y;
  transform_pristine->matrix[2 * 4 + 3] += z;

  transformed = Translate(std::move(transformed), x, y, z);
  Translate(transform.get(), x, y, z);

  ExpectTransformsAreFloatEq(transform_pristine, transformed);
  ExpectTransformsAreFloatEq(transform_pristine, transform);
}

TEST(TransformFunctionsTest, Scale) {
  const float x = 2.5;
  const float y = -10.2;
  const float z = -7.3;

  TransformPtr transform = CreateTestTransform();
  TransformPtr transform_pristine = transform->Clone();
  TransformPtr transformed = transform->Clone();

  transform_pristine->matrix[0 * 4 + 0] *= x;
  transform_pristine->matrix[1 * 4 + 1] *= y;
  transform_pristine->matrix[2 * 4 + 2] *= z;

  transformed = Scale(std::move(transformed), x, y, z);
  Scale(transform.get(), x, y, z);

  ExpectTransformsAreFloatEq(transform_pristine, transformed);
  ExpectTransformsAreFloatEq(transform_pristine, transform);
}

TEST(TransformFunctionsTest, CreateIdentityTransform) {
  TransformPtr identity = CreateTransformFromData(
      {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}});

  ExpectTransformsAreFloatEq(identity, CreateIdentityTransform());
}

TEST(TransformFunctionsTest, CreateTranslationTransform) {
  const float x = -0.5;
  const float y = 123.2;
  const float z = -9.2;

  TransformPtr translation = CreateTransformFromData(
      {{1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1}});

  ExpectTransformsAreFloatEq(translation, CreateTranslationTransform(x, y, z));
}

TEST(TransformFunctionsTest, CreateScaleTransform) {
  const float x = 0.5;
  const float y = 10.2;
  const float z = -1.5;

  TransformPtr translation = CreateTransformFromData(
      {{x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1}});

  ExpectTransformsAreFloatEq(translation, CreateScaleTransform(x, y, z));
}

}  // namespace
}  // namespace mozart
