// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/transaction/writeback.h>

#include <zircon/assert.h>

#include <zxtest/zxtest.h>

namespace fs {
namespace {

// Number of device blocks per operation block.
constexpr uint32_t kDiskBlockRatio = 2;

class MockTransactionHandler : public TransactionHandler {
 public:
  using TransactionCallback =
      fit::function<zx_status_t(const block_fifo_request_t* requests, size_t count)>;

  MockTransactionHandler() = default;

  virtual ~MockTransactionHandler() { EXPECT_EQ(transactions_expected_, transactions_seen_); }

  void SetTransactionCallbacks(TransactionCallback* callbacks, size_t expected) {
    callbacks_ = callbacks;
    transactions_expected_ = expected;
    transactions_seen_ = 0;
  }

  uint32_t FsBlockSize() const final {
    return DeviceBlockSize() * static_cast<uint32_t>(kDiskBlockRatio);
  }

  groupid_t BlockGroupID() final { return 1; }

  uint32_t DeviceBlockSize() const final { return 8192; }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final {
    return block_num;
  }

  zx_status_t RunOperation(const Operation& operation, BlockBuffer* buffer) final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  block_client::BlockDevice* GetDevice() final { return nullptr; }

  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) override {
    EXPECT_LT(transactions_seen_, transactions_expected_);
    return callbacks_[transactions_seen_++](requests, count);
  }

 private:
  TransactionCallback* callbacks_ = nullptr;
  size_t transactions_expected_ = 0;
  size_t transactions_seen_ = 0;
};

TEST(FlushRequestsTest, FlushNoRequests) {
  class TestTransactionHandler : public MockTransactionHandler {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      ZX_ASSERT_MSG(false, "Zero requests should not invoke the Transaction operation");
    }
  } handler;
  fbl::Vector<BufferedOperation> operations;
  EXPECT_OK(FlushWriteRequests(&handler, operations));
}

TEST(FlushRequestsTest, FlushOneRequest) {
  static constexpr vmoid_t kVmoid = 4;
  class TestTransactionHandler : public MockTransactionHandler {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      EXPECT_EQ(1, count);
      EXPECT_EQ(1 * kDiskBlockRatio, requests[0].vmo_offset);
      EXPECT_EQ(2 * kDiskBlockRatio, requests[0].dev_offset);
      EXPECT_EQ(3 * kDiskBlockRatio, requests[0].length);
      EXPECT_EQ(kVmoid, requests[0].vmoid);
      return ZX_OK;
    }
  } handler;
  fbl::Vector<BufferedOperation> operations;
  operations.push_back(BufferedOperation{kVmoid, Operation{OperationType::kWrite, 1, 2, 3}});
  EXPECT_OK(FlushWriteRequests(&handler, operations));
}

TEST(FlushRequestsTest, FlushManyRequests) {
  static constexpr vmoid_t kVmoidA = 7;
  static constexpr vmoid_t kVmoidB = 8;
  class TestTransactionHandler : public MockTransactionHandler {
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
  } handler;
  fbl::Vector<BufferedOperation> operations;
  operations.push_back(BufferedOperation{kVmoidA, Operation{OperationType::kWrite, 1, 2, 3}});
  operations.push_back(BufferedOperation{kVmoidB, Operation{OperationType::kWrite, 4, 5, 6}});
  EXPECT_OK(FlushWriteRequests(&handler, operations));
}

// This acts as a regression test against a previous implementation of
// "FlushWriteRequests", which could pop the stack with a large enough number
// of requests. The new implementation utilizes heap allocation when necessary,
// and should be able to withstand very large request counts.
TEST(FlushRequestsTest, FlushAVeryLargeNumberOfRequests) {
  static constexpr vmoid_t kVmoid = 7;
  static constexpr size_t kOperationCount = 10000;
  class TestTransactionHandler : public MockTransactionHandler {
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
  } handler;

  fbl::Vector<BufferedOperation> operations;
  for (size_t i = 0; i < kOperationCount; i++) {
    operations.push_back(
        BufferedOperation{kVmoid, Operation{OperationType::kWrite, i * 2, i * 2, 1}});
  }
  EXPECT_OK(FlushWriteRequests(&handler, operations));
}

TEST(FlushRequestsTest, BadFlush) {
  class TestTransactionHandler : public MockTransactionHandler {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      return ZX_ERR_NOT_SUPPORTED;
    }
  } handler;
  fbl::Vector<BufferedOperation> operations;
  operations.push_back(BufferedOperation{1, Operation{OperationType::kWrite, 1, 2, 3}});
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, FlushWriteRequests(&handler, operations));
}

}  // namespace
}  // namespace fs
