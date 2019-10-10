// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/blobfs.h>

#include <blobfs/format.h>
#include <block-client/cpp/fake-device.h>
#include <storage/buffer/vmo-buffer.h>
#include <zxtest/zxtest.h>

#include "utils.h"

namespace blobfs {
namespace {

using block_client::BlockDevice;
using block_client::FakeBlockDevice;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

std::unique_ptr<FakeBlockDevice> CreateAndFormatDevice() {
  auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
  EXPECT_OK(FormatFilesystem(device.get()));
  if (CURRENT_TEST_HAS_FAILURES()) {
    return nullptr;
  }
  return device;
}

class BlobfsTest : public zxtest::Test {
 public:
  void SetUp() final {
    MountOptions options;
    std::unique_ptr<FakeBlockDevice> device = CreateAndFormatDevice();
    ASSERT_TRUE(device);
    device_ = device.get();
    ASSERT_OK(Blobfs::Create(std::move(device), &options, &fs_));
  }

 protected:
  FakeBlockDevice* device_ = nullptr;
  std::unique_ptr<Blobfs> fs_;
};

TEST_F(BlobfsTest, GetDevice) { ASSERT_EQ(device_, fs_->GetDevice()); }

TEST_F(BlobfsTest, BlockNumberToDevice) {
  ASSERT_EQ(42 * kBlobfsBlockSize / kBlockSize, fs_->BlockNumberToDevice(42));
}

TEST_F(BlobfsTest, CleanFlag) {
  storage::VmoBuffer buffer;
  ASSERT_OK(buffer.Initialize(fs_.get(), 1, kBlobfsBlockSize, "source"));

  // Write the superblock with the clean flag unset on Blobfs::Create in Setup.
  storage::Operation operation = {};
  memcpy(buffer.Data(0), &fs_->Info(), sizeof(Superblock));
  operation.type = storage::OperationType::kWrite;
  operation.dev_offset = 0;
  operation.length = 1;

  ASSERT_OK(fs_->RunOperation(operation, &buffer));

  // Read the superblock with the clean flag unset.
  operation.type = storage::OperationType::kRead;
  ASSERT_OK(fs_->RunOperation(operation, &buffer));

  // Check if superblock on-disk flags are marked "dirty".
  Superblock* info = reinterpret_cast<Superblock*>(buffer.Data(0));
  EXPECT_EQ(0, (info->flags & kBlobFlagClean));

  // Call shutdown to set the clean flag again.
  fs_->Shutdown(nullptr);

  // fs_->Shutdown(nullptr) will set the clean flags field, but it simply queues the writes
  // and doesn't explicitly write it to the disk. Explicitly writing the changed superblock to disk.
  operation.type = storage::OperationType::kWrite;
  operation.dev_offset = 0;
  operation.length = 1;
  memcpy(buffer.Data(0), &fs_->Info(), sizeof(Superblock));
  ASSERT_OK(fs_->RunOperation(operation, &buffer));

  // Read the superblock and confirm the clean flag is set on shutdown.
  memset(buffer.Data(0), 0, kBlobfsBlockSize);
  operation.type = storage::OperationType::kRead;
  operation.length = 1;
  ASSERT_OK(fs_->RunOperation(operation, &buffer));
  info = reinterpret_cast<Superblock*>(buffer.Data(0));
  EXPECT_EQ(kBlobFlagClean, (info->flags & kBlobFlagClean));
}

// Tests reading a well known location.
TEST_F(BlobfsTest, RunOperationExpectedRead) {
  storage::VmoBuffer buffer;
  ASSERT_OK(buffer.Initialize(fs_.get(), 1, kBlobfsBlockSize, "source"));

  // Read the first block.
  storage::Operation operation = {};
  operation.type = storage::OperationType::kRead;
  operation.length = 1;
  ASSERT_OK(fs_->RunOperation(operation, &buffer));

  uint64_t* data = reinterpret_cast<uint64_t*>(buffer.Data(0));
  EXPECT_EQ(kBlobfsMagic0, data[0]);
  EXPECT_EQ(kBlobfsMagic1, data[1]);
}

// Tests that we can read back what we write.
TEST_F(BlobfsTest, RunOperationReadWrite) {
  char data[kBlobfsBlockSize] = "something to test";

  storage::VmoBuffer buffer;
  ASSERT_OK(buffer.Initialize(fs_.get(), 1, kBlobfsBlockSize, "source"));
  memcpy(buffer.Data(0), data, kBlobfsBlockSize);

  storage::Operation operation = {};
  operation.type = storage::OperationType::kWrite;
  operation.dev_offset = 1;
  operation.length = 1;

  ASSERT_OK(fs_->RunOperation(operation, &buffer));

  memset(buffer.Data(0), 'a', kBlobfsBlockSize);
  operation.type = storage::OperationType::kRead;
  ASSERT_OK(fs_->RunOperation(operation, &buffer));

  ASSERT_BYTES_EQ(data, buffer.Data(0), kBlobfsBlockSize);
}

}  // namespace
}  // namespace blobfs
