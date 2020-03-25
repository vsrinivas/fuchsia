// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/transaction/buffered_operations_builder.h"

#include <storage/buffer/block_buffer.h>
#include <fs/transaction/block_transaction.h>
#include <zxtest/zxtest.h>

namespace {

using fs::BufferedOperationsBuilder;
using storage::Operation;
using storage::BlockBuffer;

TEST(BufferedOperationsBuilderTest, NoRequest) {
  BufferedOperationsBuilder builder(nullptr);

  auto requests = builder.TakeOperations();
  EXPECT_TRUE(requests.empty());
}

class MockTransactionHandler : public fs::TransactionHandler {
 public:
  MockTransactionHandler() {}
  ~MockTransactionHandler() override {}

  const Operation* operation() const { return operation_; }
  const BlockBuffer* buffer() const { return buffer_; }

  uint32_t FsBlockSize() const final { return 0; }
  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return 0; }

  zx_status_t RunOperation(const Operation& operation, BlockBuffer* buffer) final {
    operation_ = &operation;
    buffer_ = buffer;
    return ZX_OK;
  }

  zx_status_t Readblk(uint32_t bno, void* data) final { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t Writeblk(uint32_t bno, const void* data) final { return ZX_ERR_NOT_SUPPORTED; }

 private:
  const Operation* operation_ = nullptr;
  const BlockBuffer* buffer_ = nullptr;
};

class MockBuffer final : public storage::BlockBuffer {
 public:
  MockBuffer() {}
  ~MockBuffer() override {}

  size_t capacity() const final { return 0; }
  uint32_t BlockSize() const final { return 0; }
  vmoid_t vmoid() const final { return 0; }
  void* Data(size_t index) final { return nullptr; }
  const void* Data(size_t index) const final { return nullptr; }
};

TEST(BufferedOperationsBuilderTest, ForwardsRequest) {
  MockTransactionHandler handler;
  BufferedOperationsBuilder builder(&handler);

  MockBuffer buffer;
  Operation operation = {};
  builder.Add(operation, &buffer);

  // Verify that the implementation simply forwards the request to the handler.
  EXPECT_EQ(&operation, handler.operation());
  EXPECT_EQ(&buffer, handler.buffer());

  auto requests = builder.TakeOperations();
  ASSERT_EQ(0, requests.size());
}

}  // namespace
