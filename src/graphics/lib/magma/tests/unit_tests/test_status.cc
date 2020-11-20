// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "magma_util/status.h"

#if defined(__Fuchsia__)
#include <fuchsia/gpu/magma/llcpp/fidl.h>
using FidlStatus = llcpp::fuchsia::gpu::magma::Status;

static FidlStatus Convert(magma_status_t status) {
  return static_cast<FidlStatus>(magma::Status(status).getFidlStatus());
}
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
TEST(MagmaUtil, FidlStatus) {
  EXPECT_EQ(Convert(MAGMA_STATUS_INTERNAL_ERROR), FidlStatus::INTERNAL_ERROR);
  EXPECT_EQ(Convert(MAGMA_STATUS_INVALID_ARGS), FidlStatus::INVALID_ARGS);
  EXPECT_EQ(Convert(MAGMA_STATUS_ACCESS_DENIED), FidlStatus::ACCESS_DENIED);
  EXPECT_EQ(Convert(MAGMA_STATUS_MEMORY_ERROR), FidlStatus::MEMORY_ERROR);
  EXPECT_EQ(Convert(MAGMA_STATUS_CONTEXT_KILLED), FidlStatus::CONTEXT_KILLED);
  EXPECT_EQ(Convert(MAGMA_STATUS_CONNECTION_LOST), FidlStatus::CONNECTION_LOST);
  EXPECT_EQ(Convert(MAGMA_STATUS_TIMED_OUT), FidlStatus::TIMED_OUT);
  EXPECT_EQ(Convert(MAGMA_STATUS_UNIMPLEMENTED), FidlStatus::UNIMPLEMENTED);
  EXPECT_EQ(Convert(MAGMA_STATUS_BAD_STATE), FidlStatus::BAD_STATE);
  EXPECT_EQ(MAGMA_STATUS_ALIAS_FOR_LAST, MAGMA_STATUS_BAD_STATE) << "test needs updating";
}
#endif
