// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/bcache.h"

#include <fcntl.h>
#include <unistd.h>

#include <vector>

#include <storage/buffer/block_buffer.h>
#include <zxtest/zxtest.h>

#include "src/storage/minfs/format.h"

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
  zx_handle_t Vmo() const final { return ZX_HANDLE_INVALID; }
  void* Data(size_t index) final { return &data_[index * kMinfsBlockSize]; }
  const void* Data(size_t index) const final { return &data_[index * kMinfsBlockSize]; }

 private:
  std::vector<char> data_;
};

class BcacheTest : public zxtest::Test {
 public:
  void SetUp() final {
    static constexpr char kFile[] = "/tmp/minfs_host_bcache_test.dat";
    unlink(kFile);
    fbl::unique_fd file(open(kFile, O_RDWR | O_CREAT, 0666));
    ASSERT_TRUE(file);

    ASSERT_OK(minfs::Bcache::Create(std::move(file), kNumBlocks, &bcache_));
  }

  void TearDown() final { bcache_.reset(); }

 protected:
  std::unique_ptr<minfs::Bcache> bcache_;
};

TEST_F(BcacheTest, BlockNumberToDevice) { ASSERT_EQ(42, bcache_->BlockNumberToDevice(42)); }

TEST_F(BcacheTest, RunOperation) {
  DataBuffer buffer(4);

  // Prepare to write from the end of the buffer.
  memset(buffer.Data(2), '2', buffer.BlockSize());
  memset(buffer.Data(3), '3', buffer.BlockSize());

  storage::Operation operation = {};
  operation.type = storage::OperationType::kWrite;
  operation.vmo_offset = 2;
  operation.dev_offset = 1;
  operation.length = 2;

  ASSERT_OK(bcache_->RunOperation(operation, &buffer));

  // Now read back at the start of the buffer.
  operation.type = storage::OperationType::kRead;
  operation.vmo_offset = 0;

  ASSERT_OK(bcache_->RunOperation(operation, &buffer));
  EXPECT_BYTES_EQ(buffer.Data(2), buffer.Data(0), buffer.BlockSize() * 2);
}

}  // namespace
