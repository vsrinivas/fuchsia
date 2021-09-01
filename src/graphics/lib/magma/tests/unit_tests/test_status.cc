// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "magma_util/status.h"

#if defined(__Fuchsia__)
#include <fidl/fuchsia.gpu.magma/cpp/wire.h>
using FidlStatus = fuchsia_gpu_magma::wire::Status;

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
  EXPECT_EQ(Convert(MAGMA_STATUS_INTERNAL_ERROR), FidlStatus::kInternalError);
  EXPECT_EQ(Convert(MAGMA_STATUS_INVALID_ARGS), FidlStatus::kInvalidArgs);
  EXPECT_EQ(Convert(MAGMA_STATUS_ACCESS_DENIED), FidlStatus::kAccessDenied);
  EXPECT_EQ(Convert(MAGMA_STATUS_MEMORY_ERROR), FidlStatus::kMemoryError);
  EXPECT_EQ(Convert(MAGMA_STATUS_CONTEXT_KILLED), FidlStatus::kContextKilled);
  EXPECT_EQ(Convert(MAGMA_STATUS_CONNECTION_LOST), FidlStatus::kConnectionLost);
  EXPECT_EQ(Convert(MAGMA_STATUS_TIMED_OUT), FidlStatus::kTimedOut);
  EXPECT_EQ(Convert(MAGMA_STATUS_UNIMPLEMENTED), FidlStatus::kUnimplemented);
  EXPECT_EQ(Convert(MAGMA_STATUS_BAD_STATE), FidlStatus::kBadState);
  EXPECT_EQ(MAGMA_STATUS_ALIAS_FOR_LAST, MAGMA_STATUS_BAD_STATE) << "test needs updating";
}
#endif
