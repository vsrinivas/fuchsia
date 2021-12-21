// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/bcache.h"

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>
#include <storage/buffer/vmo_buffer.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs.h"

namespace minfs {
namespace {

using block_client::BlockDevice;
using block_client::FakeBlockDevice;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 64;

class MockBlockDevice : public FakeBlockDevice {
 public:
  MockBlockDevice() : FakeBlockDevice(kNumBlocks, kBlockSize) {}
  ~MockBlockDevice() {}

  void Reset() {
    called_ = false;
    request_ = {};
  }

  block_fifo_request_t request() const { return request_; }

  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) final {
    if (count != 1 || called_) {
      return ZX_ERR_IO_REFUSED;
    }
    called_ = true;
    request_ = *requests;
    return ZX_OK;
  }

 private:
  block_fifo_request_t request_ = {};
  bool called_ = false;
};

class BcacheTestWithMockDevice : public testing::Test {
 public:
  void SetUp() final {
    auto device = std::make_unique<MockBlockDevice>();
    ASSERT_TRUE(device);
    device_ = device.get();

    auto bcache_or = Bcache::Create(std::move(device), kNumBlocks);
    ASSERT_TRUE(bcache_or.is_ok());
    bcache_ = std::move(bcache_or.value());
  }

 protected:
  MockBlockDevice* device_ = nullptr;
  std::unique_ptr<Bcache> bcache_;
};

TEST_F(BcacheTestWithMockDevice, GetDevice) { ASSERT_EQ(device_, bcache_->GetDevice()); }

TEST_F(BcacheTestWithMockDevice, BlockNumberToDevice) {
  ASSERT_EQ(42 * kMinfsBlockSize / kBlockSize, bcache_->BlockNumberToDevice(42));
}

TEST_F(BcacheTestWithMockDevice, RunOperation) {
  storage::VmoBuffer buffer;
  ASSERT_EQ(buffer.Initialize(bcache_.get(), 1, kMinfsBlockSize, "source"), ZX_OK);

  const uint64_t kVmoOffset = 1234;
  const uint64_t kDeviceOffset = 42;
  const uint64_t kLength = 5678;

  storage::Operation operation = {};
  operation.type = storage::OperationType::kWrite;
  operation.vmo_offset = kVmoOffset;
  operation.dev_offset = kDeviceOffset;
  operation.length = kLength;

  ASSERT_EQ(bcache_->RunOperation(operation, &buffer), ZX_OK);

  block_fifo_request_t request = device_->request();
  ASSERT_EQ(request.opcode, unsigned{BLOCKIO_WRITE});
  ASSERT_EQ(buffer.vmoid(), request.vmoid);
  ASSERT_EQ(bcache_->BlockNumberToDevice(kVmoOffset), request.vmo_offset);
  ASSERT_EQ(bcache_->BlockNumberToDevice(kDeviceOffset), request.dev_offset);
  ASSERT_EQ(bcache_->BlockNumberToDevice(kLength), request.length);

  operation.type = storage::OperationType::kRead;
  device_->Reset();

  ASSERT_EQ(bcache_->RunOperation(operation, &buffer), ZX_OK);

  request = device_->request();
  ASSERT_EQ(request.opcode, unsigned{BLOCKIO_READ});
  ASSERT_EQ(buffer.vmoid(), request.vmoid);
  ASSERT_EQ(bcache_->BlockNumberToDevice(kVmoOffset), request.vmo_offset);
  ASSERT_EQ(bcache_->BlockNumberToDevice(kDeviceOffset), request.dev_offset);
  ASSERT_EQ(bcache_->BlockNumberToDevice(kLength), request.length);
}

TEST(BcacheTest, WriteblkThenReadblk) {
  auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
  auto bcache_or = Bcache::Create(std::move(device), kNumBlocks);
  ASSERT_TRUE(bcache_or.is_ok());
  std::unique_ptr<uint8_t[]> source_buffer(new uint8_t[kMinfsBlockSize]);

  // Write 'a' to block 1.
  memset(source_buffer.get(), 'a', kMinfsBlockSize);
  ASSERT_TRUE(bcache_or->Writeblk(1, source_buffer.get()).is_ok());

  // Write 'b' to block 2.
  memset(source_buffer.get(), 'b', kMinfsBlockSize);
  ASSERT_TRUE(bcache_or->Writeblk(2, source_buffer.get()).is_ok());

  std::unique_ptr<uint8_t[]> destination_buffer(new uint8_t[kMinfsBlockSize]);
  // Read 'a' from block 1.
  memset(source_buffer.get(), 'a', kMinfsBlockSize);
  ASSERT_TRUE(bcache_or->Readblk(1, destination_buffer.get()).is_ok());
  EXPECT_EQ(memcmp(source_buffer.get(), destination_buffer.get(), kMinfsBlockSize), 0);

  // Read 'b' from block 2.
  memset(source_buffer.get(), 'b', kMinfsBlockSize);
  ASSERT_TRUE(bcache_or->Readblk(2, destination_buffer.get()).is_ok());
  EXPECT_EQ(memcmp(source_buffer.get(), destination_buffer.get(), kMinfsBlockSize), 0);
}

}  // namespace
}  // namespace minfs
