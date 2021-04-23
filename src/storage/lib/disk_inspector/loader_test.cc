// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "disk_inspector/loader.h"

#include <cstring>

#include <gtest/gtest.h>
#include <storage/buffer/array_buffer.h>

#include "src/lib/storage/vfs/cpp/transaction/transaction_handler.h"

namespace disk_inspector {
namespace {

constexpr uint64_t kTestBlockSize = 8192;

class MockTransactionHandler : public fs::TransactionHandler {
 public:
  explicit MockTransactionHandler(storage::ArrayBuffer* mock_device) : mock_device_(mock_device) {}
  MockTransactionHandler(const MockTransactionHandler&) = delete;
  MockTransactionHandler(MockTransactionHandler&&) = default;
  MockTransactionHandler& operator=(const MockTransactionHandler&) = delete;
  MockTransactionHandler& operator=(MockTransactionHandler&&) = default;

  // TransactionHandler interface:
  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return block_num; }

  zx_status_t RunRequests(const std::vector<storage::BufferedOperation>&) final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t RunOperation(const storage::Operation& operation,
                           storage::BlockBuffer* buffer) final {
    ValidateOperation(operation, buffer);
    switch (operation.type) {
      case storage::OperationType::kRead:
        memcpy(buffer->Data(operation.vmo_offset), mock_device_->Data(operation.dev_offset),
               operation.length * mock_device_->BlockSize());
        break;
      case storage::OperationType::kWrite:
        memcpy(mock_device_->Data(operation.dev_offset), buffer->Data(operation.vmo_offset),
               operation.length * mock_device_->BlockSize());
        break;
      default:
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
  }

  void ValidateOperation(const storage::Operation& operation, storage::BlockBuffer* buffer) {
    ASSERT_NE(nullptr, mock_device_);
    ASSERT_GE(buffer->capacity(), operation.vmo_offset + operation.length);
    ASSERT_GE(mock_device_->capacity(), operation.dev_offset + operation.length);

    ASSERT_NE(operation.type, storage::OperationType::kTrim);
  }

 private:
  storage::ArrayBuffer* mock_device_;
};

TEST(InspectorLoader, RunReadOperation) {
  uint64_t block_length = 3;

  storage::ArrayBuffer device(block_length, kTestBlockSize);
  memset(device.Data(0), 'a', device.BlockSize());
  memset(device.Data(1), 'b', device.BlockSize());
  memset(device.Data(2), 'c', device.BlockSize());

  MockTransactionHandler handler(&device);
  Loader loader(&handler);

  storage::ArrayBuffer client_buffer(block_length, kTestBlockSize);
  memset(client_buffer.Data(0), 'd', client_buffer.capacity() * device.BlockSize());
  ASSERT_EQ(ZX_OK, loader.RunReadOperation(&client_buffer, 0, 0, 1));
  ASSERT_EQ(ZX_OK, loader.RunReadOperation(&client_buffer, 2, 2, 1));

  storage::ArrayBuffer expected(block_length, kTestBlockSize);
  memset(expected.Data(0), 'a', expected.BlockSize());
  memset(expected.Data(1), 'd', expected.BlockSize());
  memset(expected.Data(2), 'c', expected.BlockSize());
  EXPECT_EQ(0, std::memcmp(client_buffer.Data(0), expected.Data(0), kTestBlockSize * block_length));
}

TEST(InspectorLoader, RunReadOperationBufferSizeAssertFail) {
  uint64_t block_length = 2;

  storage::ArrayBuffer device(block_length, kTestBlockSize);
  MockTransactionHandler handler(&device);
  Loader loader(&handler);

  storage::ArrayBuffer client_buffer(0, kTestBlockSize);
  // Buffer too small.
  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL, loader.RunReadOperation(&client_buffer, 0, 0, block_length));
}

TEST(InspectorLoader, RunWriteOperation) {
  uint64_t block_length = 3;

  storage::ArrayBuffer device(block_length, kTestBlockSize);
  memset(device.Data(0), 'a', device.BlockSize());
  memset(device.Data(1), 'b', device.BlockSize());
  memset(device.Data(2), 'c', device.BlockSize());

  MockTransactionHandler handler(&device);
  Loader loader(&handler);

  storage::ArrayBuffer client_buffer(block_length, kTestBlockSize);
  memset(client_buffer.Data(0), 'd', client_buffer.capacity() * device.BlockSize());
  ASSERT_EQ(ZX_OK, loader.RunWriteOperation(&client_buffer, 0, 0, 1));
  ASSERT_EQ(ZX_OK, loader.RunWriteOperation(&client_buffer, 2, 2, 1));

  storage::ArrayBuffer expected(block_length, kTestBlockSize);
  memset(expected.Data(0), 'd', expected.BlockSize());
  memset(expected.Data(1), 'b', expected.BlockSize());
  memset(expected.Data(2), 'd', expected.BlockSize());
  EXPECT_EQ(0, std::memcmp(device.Data(0), expected.Data(0), kTestBlockSize * block_length));
}

TEST(InspectorLoader, RunWriteOperationBufferSizeAssertFail) {
  uint64_t block_length = 2;

  storage::ArrayBuffer device(block_length, kTestBlockSize);
  MockTransactionHandler handler(&device);
  Loader loader(&handler);

  storage::ArrayBuffer client_buffer(0, kTestBlockSize);
  // Buffer too small.
  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL, loader.RunReadOperation(&client_buffer, 0, 0, block_length));
}

}  // namespace
}  // namespace disk_inspector
