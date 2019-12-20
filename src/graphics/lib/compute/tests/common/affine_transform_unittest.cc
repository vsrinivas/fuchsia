// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "affine_transform.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <iostream>

// Helper function to print an affine_transform_t
static std::ostream &
operator<<(std::ostream & os, const affine_transform_t & t)
{
  os << "[sx:" << t.sx;
  if (t.shx)
    os << ",shx:" << t.shx;
  if (t.shy)
    os << ",shy:" << t.shy;
  os << ",sy:" << t.sy;
  if (t.tx || t.ty)
    os << ",tx:" << t.tx << ",ty:" << t.ty;
  os << "]";
  return os;
}

TEST(affine_transform, identity)
{
  affine_transform_t t = affine_transform_identity;
  EXPECT_DOUBLE_EQ(t.sx, 1.);
  EXPECT_DOUBLE_EQ(t.shx, 0.);
  EXPECT_DOUBLE_EQ(t.shy, 0.);
  EXPECT_DOUBLE_EQ(t.sy, 1.);
  EXPECT_DOUBLE_EQ(t.tx, 0.);
  EXPECT_DOUBLE_EQ(t.ty, 0.);

  affine_transform_t t2 = affine_transform_multiply(&t, &t);
  EXPECT_DOUBLE_EQ(t2.sx, 1.);
  EXPECT_DOUBLE_EQ(t2.shx, 0.);
  EXPECT_DOUBLE_EQ(t2.shy, 0.);
  EXPECT_DOUBLE_EQ(t2.sy, 1.);
  EXPECT_DOUBLE_EQ(t2.tx, 0.);
  EXPECT_DOUBLE_EQ(t2.ty, 0.);

  double x = 42;
  double y = 100;
  affine_transform_apply(&t, &x, &y);
  EXPECT_DOUBLE_EQ(x, 42.);
  EXPECT_DOUBLE_EQ(y, 100.);
}

//
// affine_transform_apply()
//

static ::testing::AssertionResult
TransformApplyCheck(const char *               t_expr,
                    const char *               x_expr,
                    const char *               y_expr,
                    const affine_transform_t & t,
                    double                     x,
                    double                     y)
{
  double expected_x = x * t.sx + y * t.shx + t.tx;
  double expected_y = x * t.shy + y * t.sy + t.ty;

  double x0 = x, y0 = y;
  affine_transform_apply(&t, &x, &y);

  if (x == expected_x && y == expected_y)
    return ::testing::AssertionSuccess();

  return ::testing::AssertionFailure()
         << "(" << x0 << "," << y0 << ")"
         << "transformed to (" << x << "," << y << "), but expected (" << expected_x << ","
         << expected_y << ") with transform " << t_expr << " which is " << t;
}

#define ASSERT_AFFINE_TRANSFORM_APPLY(transform_, x_, y_)                                          \
  ASSERT_PRED_FORMAT3(TransformApplyCheck, transform_, x_, y_)

#define EXPECT_AFFINE_TRANSFORM_APPLY(transform_, x_, y_)                                          \
  EXPECT_PRED_FORMAT3(TransformApplyCheck, transform_, x_, y_)

TEST(affine_transform, apply)
{
  affine_transform_t t1 = {
    .sx = 2.,
    .sy = 5.,
  };
  EXPECT_AFFINE_TRANSFORM_APPLY(t1, 0., 0.);
  EXPECT_AFFINE_TRANSFORM_APPLY(t1, 1., 0.);
  EXPECT_AFFINE_TRANSFORM_APPLY(t1, 0., 1.);
  EXPECT_AFFINE_TRANSFORM_APPLY(t1, 10., 1000.);

  affine_transform_t t2 = {
    .shx = 0.5,
    .shy = 4.,
  };
  EXPECT_AFFINE_TRANSFORM_APPLY(t2, 0., 0.);
  EXPECT_AFFINE_TRANSFORM_APPLY(t2, 1., 0.);
  EXPECT_AFFINE_TRANSFORM_APPLY(t2, 0., 1.);
  EXPECT_AFFINE_TRANSFORM_APPLY(t2, 10., 1000.);

  affine_transform_t t3 = {
    .sx  = 2.,
    .shx = 2.,
    .shy = -2.,
    .sy  = 2.,
    .tx  = 100.,
    .ty  = -200.,
  };
  EXPECT_AFFINE_TRANSFORM_APPLY(t3, 0., 0.);
  EXPECT_AFFINE_TRANSFORM_APPLY(t3, 1., 0.);
  EXPECT_AFFINE_TRANSFORM_APPLY(t3, 0., 1.);
  EXPECT_AFFINE_TRANSFORM_APPLY(t3, 10., 1000.);
}

//
// affine_transform_multiply
//

static ::testing::AssertionResult
TransformMultiplyCheck(const char *               t1_expr,
                       const char *               t2_expr,
                       const affine_transform_t & t1,
                       const affine_transform_t & t2)
{
  affine_transform_t expected = {
    .sx  = t1.sx * t2.sx + t1.shx * t2.shy,
    .shx = t1.sx * t2.shx + t1.shx * t2.sy,
    .shy = t1.shy * t2.sx + t1.sy * t2.shy,
    .sy  = t1.shy * t2.shx + t1.sy * t2.sy,
    .tx  = t1.sx * t2.tx + t1.shx * t2.ty + t1.tx,
    .ty  = t1.shy * t2.tx + t1.sy * t2.ty + t1.ty,
  };

  affine_transform_t result = affine_transform_multiply(&t1, &t2);
  if (affine_transform_equal(&result, &expected))
    return ::testing::AssertionSuccess();

  return ::testing::AssertionFailure()
         << "multiplication of " << t1_expr << " (" << t1 << ") by " << t2_expr << " (" << t2
         << ") gave " << result << ", expected " << expected;
}

#define ASSERT_AFFINE_TRANSFORM_MULTIPLY(t1_, t2_)                                                 \
  ASSERT_PRED_FORMAT2(TransformMultiplyCheck, t1_, t2_)

#define EXPECT_AFFINE_TRANSFORM_MULTIPLY(t1_, t2_)                                                 \
  EXPECT_PRED_FORMAT2(TransformMultiplyCheck, t1_, t2_)

TEST(affine_transform, multiply)
{
  affine_transform_t t1 = {
    .sx  = 10.,
    .shx = 0.66,
    .sy  = 8.,
    .tx  = 4.,
    .ty  = -2.,
  };

  affine_transform_t t2 = {
    .sx  = 3.35,
    .shy = 1.65,
    .sy  = 1.,
    .tx  = 0.,
    .ty  = -100.,
  };

  EXPECT_AFFINE_TRANSFORM_MULTIPLY(t1, t2);
}
