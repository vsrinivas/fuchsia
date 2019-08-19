// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <blobfs/writeback.h>
#include <zxtest/zxtest.h>

#include "utils.h"

namespace blobfs {
namespace {

TEST(FlushRequestsTest, FlushNoRequests) {
  class TestTransactionManager : public MockTransactionManager {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      ZX_ASSERT_MSG(false, "Zero requests should not invoke the Transaction operation");
    }
  } manager;
  fbl::Vector<BufferedOperation> operations;
  EXPECT_OK(FlushWriteRequests(&manager, operations));
}

TEST(FlushRequestsTest, FlushOneRequest) {
  static constexpr vmoid_t kVmoid = 4;
  class TestTransactionManager : public MockTransactionManager {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      EXPECT_EQ(1, count);
      EXPECT_EQ(1 * kDiskBlockRatio, requests[0].vmo_offset);
      EXPECT_EQ(2 * kDiskBlockRatio, requests[0].dev_offset);
      EXPECT_EQ(3 * kDiskBlockRatio, requests[0].length);
      EXPECT_EQ(kVmoid, requests[0].vmoid);
      return ZX_OK;
    }
  } manager;
  fbl::Vector<BufferedOperation> operations;
  operations.push_back(BufferedOperation{kVmoid, Operation{OperationType::kWrite, 1, 2, 3}});
  EXPECT_OK(FlushWriteRequests(&manager, operations));
}

TEST(FlushRequestsTest, FlushManyRequests) {
  static constexpr vmoid_t kVmoidA = 7;
  static constexpr vmoid_t kVmoidB = 8;
  class TestTransactionManager : public MockTransactionManager {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      EXPECT_EQ(2, count);
      EXPECT_EQ(1 * kDiskBlockRatio, requests[0].vmo_offset);
      EXPECT_EQ(2 * kDiskBlockRatio, requests[0].dev_offset);
      EXPECT_EQ(3 * kDiskBlockRatio, requests[0].length);
      EXPECT_EQ(4 * kDiskBlockRatio, requests[1].vmo_offset);
      EXPECT_EQ(5 * kDiskBlockRatio, requests[1].dev_offset);
      EXPECT_EQ(6 * kDiskBlockRatio, requests[1].length);
      EXPECT_EQ(kVmoidA, requests[0].vmoid);
      EXPECT_EQ(kVmoidB, requests[1].vmoid);
      return ZX_OK;
    }
  } manager;
  fbl::Vector<BufferedOperation> operations;
  operations.push_back(BufferedOperation{kVmoidA, Operation{OperationType::kWrite, 1, 2, 3}});
  operations.push_back(BufferedOperation{kVmoidB, Operation{OperationType::kWrite, 4, 5, 6}});
  EXPECT_OK(FlushWriteRequests(&manager, operations));
}

// This acts as a regression test against a previous implementation of
// "FlushWriteRequests", which could pop the stack with a large enough number
// of requests. The new implementation utilizes heap allocation when necessary,
// and should be able to withstand very large request counts.
TEST(FlushRequestsTest, FlushAVeryLargeNumberOfRequests) {
  static constexpr vmoid_t kVmoid = 7;
  static constexpr size_t kOperationCount = 10000;
  class TestTransactionManager : public MockTransactionManager {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      EXPECT_EQ(kOperationCount, count);
      for (size_t i = 0; i < count; i++) {
        EXPECT_EQ(i * 2 * kDiskBlockRatio, requests[i].vmo_offset);
        EXPECT_EQ(i * 2 * kDiskBlockRatio, requests[i].dev_offset);
        EXPECT_EQ(1 * kDiskBlockRatio, requests[i].length);
        EXPECT_EQ(kVmoid, requests[i].vmoid);
      }
      return ZX_OK;
    }
  } manager;

  fbl::Vector<BufferedOperation> operations;
  for (size_t i = 0; i < kOperationCount; i++) {
    operations.push_back(
        BufferedOperation{kVmoid, Operation{OperationType::kWrite, i * 2, i * 2, 1}});
  }
  EXPECT_OK(FlushWriteRequests(&manager, operations));
}

TEST(FlushRequestsTest, BadFlush) {
  class TestTransactionManager : public MockTransactionManager {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      return ZX_ERR_NOT_SUPPORTED;
    }
  } manager;
  fbl::Vector<BufferedOperation> operations;
  operations.push_back(BufferedOperation{1, Operation{OperationType::kWrite, 1, 2, 3}});
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, FlushWriteRequests(&manager, operations));
}

}  // namespace
}  // namespace blobfs
