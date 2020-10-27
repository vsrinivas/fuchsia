// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <fs/transaction/writeback.h>
#include <gtest/gtest.h>

namespace fs {
namespace {

// Number of device blocks per operation block.
constexpr uint32_t kDiskBlockRatio = 2;

class MockTransactionHandler : public LegacyTransactionHandler {
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

  uint32_t DeviceBlockSize() const final { return 8192; }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return block_num; }

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
  fbl::Vector<storage::BufferedOperation> operations;
  EXPECT_EQ(FlushRequests(&handler, operations), ZX_OK);
}

TEST(FlushRequestsTest, FlushOneRequest) {
  static constexpr vmoid_t kVmoid = 4;
  class TestTransactionHandler : public MockTransactionHandler {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      if (count != 1) {
        ADD_FAILURE() << "Unexpected count";
        return ZX_ERR_OUT_OF_RANGE;
      }
      EXPECT_EQ(1 * kDiskBlockRatio, requests[0].vmo_offset);
      EXPECT_EQ(2 * kDiskBlockRatio, requests[0].dev_offset);
      EXPECT_EQ(3 * kDiskBlockRatio, requests[0].length);
      EXPECT_EQ(kVmoid, requests[0].vmoid);
      return ZX_OK;
    }
  } handler;
  fbl::Vector<storage::BufferedOperation> operations;
  operations.push_back(storage::BufferedOperation{
      kVmoid, storage::Operation{storage::OperationType::kWrite, 1, 2, 3}});
  EXPECT_EQ(FlushRequests(&handler, operations), ZX_OK);
}

TEST(FlushRequestsTest, FlushManyRequests) {
  static constexpr vmoid_t kVmoidA = 7;
  static constexpr vmoid_t kVmoidB = 8;
  class TestTransactionHandler : public MockTransactionHandler {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      if (count != 2) {
        ADD_FAILURE() << "Unexpected count";
        return ZX_ERR_OUT_OF_RANGE;
      }
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
  fbl::Vector<storage::BufferedOperation> operations;
  operations.push_back(storage::BufferedOperation{
      kVmoidA, storage::Operation{storage::OperationType::kWrite, 1, 2, 3}});
  operations.push_back(storage::BufferedOperation{
      kVmoidB, storage::Operation{storage::OperationType::kWrite, 4, 5, 6}});
  EXPECT_EQ(FlushRequests(&handler, operations), ZX_OK);
}

// This acts as a regression test against a previous implementation of
// "FlushRequests", which could pop the stack with a large enough number
// of requests. The new implementation utilizes heap allocation when necessary,
// and should be able to withstand very large request counts.
TEST(FlushRequestsTest, FlushAVeryLargeNumberOfRequests) {
  static constexpr vmoid_t kVmoid = 7;
  static constexpr size_t kOperationCount = 10000;
  class TestTransactionHandler : public MockTransactionHandler {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      if (count != kOperationCount) {
        ADD_FAILURE() << "Unexpected count";
        return ZX_ERR_OUT_OF_RANGE;
      }
      for (size_t i = 0; i < count; i++) {
        EXPECT_EQ(i * 2 * kDiskBlockRatio, requests[i].vmo_offset);
        EXPECT_EQ(i * 2 * kDiskBlockRatio, requests[i].dev_offset);
        EXPECT_EQ(1 * kDiskBlockRatio, requests[i].length);
        EXPECT_EQ(kVmoid, requests[i].vmoid);
      }
      return ZX_OK;
    }
  } handler;

  fbl::Vector<storage::BufferedOperation> operations;
  for (size_t i = 0; i < kOperationCount; i++) {
    operations.push_back(storage::BufferedOperation{
        kVmoid, storage::Operation{storage::OperationType::kWrite, i * 2, i * 2, 1}});
  }
  EXPECT_EQ(FlushRequests(&handler, operations), ZX_OK);
}

TEST(FlushRequestsTest, BadFlush) {
  class TestTransactionHandler : public MockTransactionHandler {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      return ZX_ERR_NOT_SUPPORTED;
    }
  } handler;
  fbl::Vector<storage::BufferedOperation> operations;
  operations.push_back(
      storage::BufferedOperation{1, storage::Operation{storage::OperationType::kWrite, 1, 2, 3}});
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, FlushRequests(&handler, operations));
}

TEST(FlushRequestsTest, FlushTrimRequest) {
  static constexpr vmoid_t kVmoid = 4;
  class TestTransactionHandler : public MockTransactionHandler {
    zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
      if (count != 1) {
        ADD_FAILURE() << "Unexpected count";
        return ZX_ERR_OUT_OF_RANGE;
      }
      EXPECT_EQ(unsigned{BLOCKIO_TRIM}, requests[0].opcode);
      EXPECT_EQ(2 * kDiskBlockRatio, requests[0].dev_offset);
      EXPECT_EQ(3 * kDiskBlockRatio, requests[0].length);
      return ZX_OK;
    }
  } handler;
  fbl::Vector<storage::BufferedOperation> operations;
  operations.push_back(storage::BufferedOperation{
      kVmoid, storage::Operation{storage::OperationType::kTrim, 1, 2, 3}});
  EXPECT_EQ(FlushRequests(&handler, operations), ZX_OK);
}

}  // namespace
}  // namespace fs
