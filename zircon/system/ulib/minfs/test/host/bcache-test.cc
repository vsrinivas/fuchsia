// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minfs/bcache.h"

#include <fcntl.h>
#include <unistd.h>

#include <vector>

#include <minfs/format.h>
#include <storage/buffer/block_buffer.h>
#include <zxtest/zxtest.h>

namespace {

constexpr uint32_t kNumBlocks = 20;
using minfs::kMinfsBlockSize;

class DataBuffer final : public storage::BlockBuffer {
 public:
  explicit DataBuffer(size_t blocks) : data_(blocks * kMinfsBlockSize) {}

  // BlockBuffer interface:
  size_t capacity() const final { return data_.size() / kMinfsBlockSize; }
  uint32_t BlockSize() const final { return kMinfsBlockSize; }
  vmoid_t vmoid() const final { return 0; }
  void* Data(size_t index) final { return &data_[index * kMinfsBlockSize]; }
  const void* Data(size_t index) const final { return &data_[index]; }

 private:
  std::vector<char> data_;
};

class BcacheTest : public zxtest::Test {
 public:
  void SetUp() final {
    fbl::unique_fd file(open("/tmp/minfs_host_bcache_test.dat", O_RDWR | O_CREAT, 0555));
    ASSERT_TRUE(file);

    ASSERT_OK(minfs::Bcache::Create(std::move(file), kNumBlocks, &bcache_));
  }

  void TearDown() final {
    bcache_.reset();
    unlink("/tmp/minfs_host_bcache_test.dat");
  }

 protected:
  std::unique_ptr<minfs::Bcache> bcache_;
};

TEST_F(BcacheTest, BlockNumberToDevice) { ASSERT_EQ(42, bcache_->BlockNumberToDevice(42)); }

TEST_F(BcacheTest, RunOperation) {
  DataBuffer buffer(2);

  // Prepare to write '2' from the second block.
  memset(buffer.Data(1), '2', buffer.BlockSize());

  storage::Operation operation = {};
  operation.type = storage::OperationType::kWrite;
  operation.vmo_offset = 1;
  operation.dev_offset = 2;
  operation.length = 1;

  ASSERT_OK(bcache_->RunOperation(operation, &buffer));

  // Now read back at the start of the buffer.
  operation.type = storage::OperationType::kRead;
  operation.vmo_offset = 0;

  ASSERT_OK(bcache_->RunOperation(operation, &buffer));
  EXPECT_BYTES_EQ(buffer.Data(1), buffer.Data(0), buffer.BlockSize());
}

}  // namespace
