// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "magma_util/status.h"

#if defined(__Fuchsia__)
#include "src/graphics/lib/magma/src/magma_util/platform/zircon/zircon_platform_status.h"
#endif

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

#if defined(__Fuchsia__)
TEST(MagmaUtil, ZxStatus) {
  EXPECT_EQ(magma::ToZxStatus(MAGMA_STATUS_INTERNAL_ERROR), ZX_ERR_INTERNAL);
  EXPECT_EQ(magma::ToZxStatus(MAGMA_STATUS_INVALID_ARGS), ZX_ERR_INVALID_ARGS);
  EXPECT_EQ(magma::ToZxStatus(MAGMA_STATUS_ACCESS_DENIED), ZX_ERR_ACCESS_DENIED);
  EXPECT_EQ(magma::ToZxStatus(MAGMA_STATUS_MEMORY_ERROR), ZX_ERR_NO_MEMORY);
  EXPECT_EQ(magma::ToZxStatus(MAGMA_STATUS_CONTEXT_KILLED), ZX_ERR_IO);
  EXPECT_EQ(magma::ToZxStatus(MAGMA_STATUS_CONNECTION_LOST), ZX_ERR_PEER_CLOSED);
  EXPECT_EQ(magma::ToZxStatus(MAGMA_STATUS_TIMED_OUT), ZX_ERR_TIMED_OUT);
  EXPECT_EQ(magma::ToZxStatus(MAGMA_STATUS_UNIMPLEMENTED), ZX_ERR_NOT_SUPPORTED);
  EXPECT_EQ(magma::ToZxStatus(MAGMA_STATUS_BAD_STATE), ZX_ERR_BAD_STATE);
  EXPECT_EQ(MAGMA_STATUS_ALIAS_FOR_LAST, MAGMA_STATUS_BAD_STATE) << "test needs updating";
}
#endif
