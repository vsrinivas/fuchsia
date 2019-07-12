// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "magma_util/status.h"

class TestStatus {
 public:
  static void Test() {
    EXPECT_EQ(MAGMA_STATUS_OK, magma::Status(MAGMA_STATUS_OK).get());
    EXPECT_EQ(MAGMA_STATUS_INTERNAL_ERROR, magma::Status(MAGMA_STATUS_INTERNAL_ERROR).get());
    EXPECT_TRUE(magma::Status(MAGMA_STATUS_OK));
    EXPECT_FALSE(magma::Status(MAGMA_STATUS_INTERNAL_ERROR));
  }
};

TEST(MagmaUtil, Status) { TestStatus::Test(); }
