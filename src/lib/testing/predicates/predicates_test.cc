// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"

class PredicatesTest : public ::testing::Test {
 public:
  // Declare some constants with zx_status_t values to assert on error messages.
  static constexpr zx_status_t kStatusOk = ZX_OK;
  static constexpr zx_status_t kStatusErrInternal = ZX_ERR_INTERNAL;
  static constexpr zx_status_t kStatusErrNotFound = ZX_ERR_NOT_FOUND;
};

TEST_F(PredicatesTest, CompareOk) {
  constexpr const char* kErrorMsg = "kStatusErrInternal is ZX_ERR_INTERNAL, expected ZX_OK.";
  // Test failure and error message.
  EXPECT_FATAL_FAILURE(ASSERT_OK(kStatusErrInternal), kErrorMsg);
  EXPECT_NONFATAL_FAILURE(EXPECT_OK(kStatusErrInternal), kErrorMsg);
  // Test success case.
  ASSERT_OK(kStatusOk);
  EXPECT_OK(kStatusOk);
}

TEST_F(PredicatesTest, CompareStatus) {
  constexpr const char* kErrorMsg =
      "Value of: kStatusErrNotFound\n  Actual: ZX_ERR_NOT_FOUND\nExpected: "
      "kStatusErrInternal\nWhich is: ZX_ERR_INTERNAL";
  // Test failure and error message.
  EXPECT_FATAL_FAILURE(ASSERT_STATUS(kStatusErrNotFound, kStatusErrInternal), kErrorMsg);
  EXPECT_NONFATAL_FAILURE(EXPECT_STATUS(kStatusErrNotFound, kStatusErrInternal), kErrorMsg);
  // Test success case.
  ASSERT_STATUS(kStatusErrInternal, ZX_ERR_INTERNAL);
  EXPECT_STATUS(kStatusErrInternal, ZX_ERR_INTERNAL);
}
