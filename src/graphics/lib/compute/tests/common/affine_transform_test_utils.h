// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_AFFINE_TRANSFORM_TEST_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_AFFINE_TRANSFORM_TEST_UTILS_H_

#include <gtest/gtest.h>

#include <ostream>

#include "affine_transform.h"

// Print to a standard C++ output stream.
extern std::ostream &
operator<<(std::ostream & os, const affine_transform_t & t);

::testing::AssertionResult
AssertAffineTransformEqual(const char *             m_expr,
                           const char *             n_expr,
                           const affine_transform_t m,
                           const affine_transform_t n);

#define ASSERT_AFFINE_TRANSFORM_EQ(m, n) ASSERT_PRED_FORMAT2(AssertAffineTransformEqual, (m), (n))
#define EXPECT_AFFINE_TRANSFORM_EQ(m, n) EXPECT_PRED_FORMAT2(AssertAffineTransformEqual, (m), (n))

#define ASSERT_AFFINE_TRANSFORM_IS_IDENTITY(m)                                                     \
  ASSERT_AFFINE_TRANSFORM_EQ((m), affine_transform_identity)

#define EXPECT_AFFINE_TRANSFORM_IS_IDENTITY(n)                                                     \
  EXPECT_AFFINE_TRANSFORM_EQ((m), affine_transform_identity)

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_AFFINE_TRANSFORM_TEST_UTILS_H_
