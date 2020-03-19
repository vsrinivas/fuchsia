// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_TESTING_PREDICATES_STATUS_H_
#define SRC_LIB_TESTING_PREDICATES_STATUS_H_

#include <zircon/status.h>

#include <gtest/gtest.h>

// Helper macro that asserts that `condition` equals `ZX_OK`.
// Behaves similarly to `ASSERT_EQ(condition, ZX_OK)` but with prettier output.
#define ASSERT_OK(condition) \
  GTEST_PRED_FORMAT1_(::testing_predicates::CmpZxOk, condition, GTEST_FATAL_FAILURE_)
// Helper macro that expects that condition equals `ZX_OK`.
// Behaves similarly to `EXPECT_EQ(condition, ZX_OK)` but with prettier output.
#define EXPECT_OK(condition) \
  GTEST_PRED_FORMAT1_(::testing_predicates::CmpZxOk, condition, GTEST_NONFATAL_FAILURE_)
// Helper macro that asserts equality between `zx_status_t` expressions `val1` and `val2`.
// Behaves similarly to `ASSERT_EQ(val1, val2)` but with prettier output.
#define ASSERT_STATUS(val1, val2) \
  GTEST_PRED_FORMAT2_(::testing_predicates::CmpStatus, val1, val2, GTEST_FATAL_FAILURE_)
// Helper macro that expects equality between `zx_status_t` expressions `val1` and `val2`.
// Behaves similarly to `EXPECT_EQ(val1, val2)` but with prettier output.
#define EXPECT_STATUS(val1, val2) \
  GTEST_PRED_FORMAT2_(::testing_predicates::CmpStatus, val1, val2, GTEST_NONFATAL_FAILURE_)

namespace testing_predicates {
::testing::AssertionResult CmpZxOk(const char* l_expr, zx_status_t l);
::testing::AssertionResult CmpStatus(const char* l_expr, const char* r_expr, zx_status_t l,
                                     zx_status_t r);
}  // namespace testing_predicates

#endif  // SRC_LIB_TESTING_PREDICATES_STATUS_H_
