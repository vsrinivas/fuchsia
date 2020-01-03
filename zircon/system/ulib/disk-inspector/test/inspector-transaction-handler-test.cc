// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/disk-inspector/inspector-transaction-handler.h>

#include <block-client/cpp/fake-device.h>
#include <storage/buffer/vmo_buffer.h>
#include <zxtest/zxtest.h>

namespace disk_inspector {
namespace {

using block_client::FakeBlockDevice;

constexpr uint64_t kBlockCount = 1 << 15;
constexpr uint32_t kBlockSize = 512;
constexpr uint64_t kBufferCapacity = 20;
constexpr uint64_t kDeviceOffset = 37;

TEST(InspectorTransactionHandlerTest, ConstructFailWithBlockSizeMismatch) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  // InspectorTransactionHandler block size must be a multiple of the underlying block size.
  std::unique_ptr<InspectorTransactionHandler> handler;
  ASSERT_NOT_OK(InspectorTransactionHandler::Create(std::move(device), 20, &handler));
}

TEST(InspectorTransactionHandlerTest, WriteToDeviceAndReadBack) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  std::unique_ptr<InspectorTransactionHandler> handler;
  ASSERT_OK(InspectorTransactionHandler::Create(std::move(device), kBlockSize, &handler));

  // Write operation
  storage::VmoBuffer write_buffer;
  ASSERT_OK(write_buffer.Initialize(handler.get(), kBufferCapacity, kBlockSize, "write-buffer"));

  // Fill |buffer| with some arbitrary data.
  char buf[kBlockSize * kBufferCapacity];
  memset(buf, 'a', sizeof(buf));
  for (size_t i = 0; i < kBufferCapacity; i++) {
    memcpy(write_buffer.Data(i), buf, kBlockSize);
  }

  storage::Operation write_op = {
      .type = storage::OperationType::kWrite,
      .vmo_offset = 0,
      .dev_offset = kDeviceOffset,
      .length = kBufferCapacity,
  };
  ASSERT_OK(handler->RunOperation(write_op, &write_buffer));

  // Read operation
  storage::VmoBuffer read_buffer;
  ASSERT_OK(read_buffer.Initialize(handler.get(), kBufferCapacity, kBlockSize, "read-buffer"));

  storage::Operation read_op = {
      .type = storage::OperationType::kRead,
      .vmo_offset = 0,
      .dev_offset = kDeviceOffset,
      .length = kBufferCapacity,
  };
  ASSERT_OK(handler->RunOperation(read_op, &read_buffer));

  EXPECT_BYTES_EQ(buf, read_buffer.Data(0), kBufferCapacity * kBlockSize,
                  "Read data different from device data.");
}

}  // namespace
}  // namespace disk_inspector
