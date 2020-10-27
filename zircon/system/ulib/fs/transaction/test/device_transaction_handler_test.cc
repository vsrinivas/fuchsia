// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/transaction/device_transaction_handler.h"

#include <vector>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>
#include <sanitizer/lsan_interface.h>

namespace {

using storage::BlockBuffer;
using storage::BufferedOperation;
using storage::Operation;
using storage::OperationType;

// Number of device blocks per operation block.
constexpr uint32_t kBlockRatio = 2;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 64;

class MockBlockDevice : public block_client::FakeBlockDevice {
 public:
  MockBlockDevice() : FakeBlockDevice(kNumBlocks, kBlockSize) {}
  ~MockBlockDevice() override {}

  const std::vector<block_fifo_request_t>& requests() const { return requests_; }

  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) final {
    if (called_) {
      return ZX_ERR_IO_REFUSED;
    }
    called_ = true;
    requests_.assign(requests, requests + count);
    return ZX_OK;
  }

 private:
  std::vector<block_fifo_request_t> requests_;
  bool called_ = false;
};

class MockTransactionHandler : public fs::DeviceTransactionHandler {
 public:
  MockTransactionHandler() {}
  ~MockTransactionHandler() override {}

  const std::vector<block_fifo_request_t>& GetRequests() const { return device_.requests(); }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return block_num * kBlockRatio; }

  zx_status_t RunOperation(const Operation& operation, BlockBuffer* buffer) final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  block_client::BlockDevice* GetDevice() final { return &device_; }

 private:
  MockBlockDevice device_;
};

class TransactionHandlerTest : public testing::Test {
 public:
  void SetUp() final { handler_ = std::make_unique<MockTransactionHandler>(); }

 protected:
  std::unique_ptr<MockTransactionHandler> handler_;
};

TEST_F(TransactionHandlerTest, RunRequestsNoRequests) {
  std::vector<BufferedOperation> operations;
  EXPECT_EQ(handler_->RunRequests(operations), ZX_OK);
  EXPECT_EQ(0u, handler_->GetRequests().size());
}

TEST_F(TransactionHandlerTest, RunRequestsOneRequest) {
  const vmoid_t kVmoid = 4;
  std::vector<BufferedOperation> operations = {{kVmoid, {OperationType::kWrite, 1, 2, 3}}};
  EXPECT_EQ(handler_->RunRequests(operations), ZX_OK);

  const std::vector<block_fifo_request_t>& requests = handler_->GetRequests();
  EXPECT_EQ(1u, requests.size());
  EXPECT_EQ(1 * kBlockRatio, requests[0].vmo_offset);
  EXPECT_EQ(2 * kBlockRatio, requests[0].dev_offset);
  EXPECT_EQ(3 * kBlockRatio, requests[0].length);
  EXPECT_EQ(kVmoid, requests[0].vmoid);
  EXPECT_EQ(unsigned{BLOCKIO_WRITE}, requests[0].opcode);
}

TEST_F(TransactionHandlerTest, RunRequestsTrim) {
  const vmoid_t kVmoid = 4;
  std::vector<BufferedOperation> operations = {{kVmoid, {OperationType::kTrim, 1, 2, 3}}};
  EXPECT_EQ(handler_->RunRequests(operations), ZX_OK);

  const std::vector<block_fifo_request_t>& requests = handler_->GetRequests();
  EXPECT_EQ(1u, requests.size());
  EXPECT_EQ(1 * kBlockRatio, requests[0].vmo_offset);
  EXPECT_EQ(2 * kBlockRatio, requests[0].dev_offset);
  EXPECT_EQ(3 * kBlockRatio, requests[0].length);
  EXPECT_EQ(kVmoid, requests[0].vmoid);
  EXPECT_EQ(unsigned{BLOCKIO_TRIM}, requests[0].opcode);
}

TEST_F(TransactionHandlerTest, RunRequestsManyRequests) {
  std::vector<BufferedOperation> operations;
  operations.push_back({10, {OperationType::kRead, 11, 12, 13}});
  operations.push_back({20, {OperationType::kRead, 24, 25, 26}});
  operations.push_back({30, {OperationType::kRead, 37, 38, 39}});
  EXPECT_EQ(handler_->RunRequests(operations), ZX_OK);

  const std::vector<block_fifo_request_t>& requests = handler_->GetRequests();
  EXPECT_EQ(3u, requests.size());
  EXPECT_EQ(unsigned{BLOCKIO_READ}, requests[0].opcode);
  EXPECT_EQ(10, requests[0].vmoid);
  EXPECT_EQ(11 * kBlockRatio, requests[0].vmo_offset);
  EXPECT_EQ(12 * kBlockRatio, requests[0].dev_offset);
  EXPECT_EQ(13 * kBlockRatio, requests[0].length);

  EXPECT_EQ(unsigned{BLOCKIO_READ}, requests[1].opcode);
  EXPECT_EQ(20u, requests[1].vmoid);
  EXPECT_EQ(24 * kBlockRatio, requests[1].vmo_offset);
  EXPECT_EQ(25 * kBlockRatio, requests[1].dev_offset);
  EXPECT_EQ(26 * kBlockRatio, requests[1].length);

  EXPECT_EQ(unsigned{BLOCKIO_READ}, requests[2].opcode);
  EXPECT_EQ(30u, requests[2].vmoid);
  EXPECT_EQ(37 * kBlockRatio, requests[2].vmo_offset);
  EXPECT_EQ(38 * kBlockRatio, requests[2].dev_offset);
  EXPECT_EQ(39 * kBlockRatio, requests[2].length);
}

TEST_F(TransactionHandlerTest, RunRequestsFails) {
  std::vector<BufferedOperation> operations = {{0, {OperationType::kWrite, 1, 2, 3}}};
  EXPECT_EQ(handler_->RunRequests(operations), ZX_OK);

  EXPECT_NE(handler_->RunRequests(operations), ZX_OK);
}

TEST_F(TransactionHandlerTest, FlushCallsFlush) {
  handler_->Flush();
  const std::vector<block_fifo_request_t>& requests = handler_->GetRequests();
  EXPECT_EQ(1u, requests.size());
  EXPECT_EQ(unsigned{BLOCKIO_FLUSH}, requests[0].opcode);
}

#if ZX_DEBUG_ASSERT_IMPLEMENTED

using TransactionHandlerCrashTest = TransactionHandlerTest;

TEST_F(TransactionHandlerCrashTest, RunRequestsMixedRequests) {
  std::vector<BufferedOperation> operations;
  operations.push_back({10, {OperationType::kRead, 11, 12, 13}});
  operations.push_back({20, {OperationType::kWrite, 24, 25, 26}});
  ASSERT_DEATH(
      {
#if __has_feature(address_sanitizer) || __has_feature(leak_sanitizer)
        // Disable LSAN, this thread is expected to leak by way of a crash.
        __lsan::ScopedDisabler _;
#endif
        handler_->RunRequests(operations);
      },
      "");
}

#endif  // ZX_DEBUG_ASSERT_IMPLEMENTED

}  // namespace
