// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs.h"

#include <lib/sync/completion.h>

#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>
#include <storage/buffer/vmo_buffer.h>

#include "directory.h"
#include "test/blob_utils.h"

namespace blobfs {
namespace {

using block_client::FakeBlockDevice;

class MockBlockDevice : public FakeBlockDevice {
 public:
  MockBlockDevice(uint64_t block_count, uint32_t block_size)
      : FakeBlockDevice(block_count, block_size) {}

  bool saw_trim() const { return saw_trim_; }

  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) final;
  zx_status_t BlockGetInfo(fuchsia_hardware_block_BlockInfo* info) const final;

 private:
  bool saw_trim_ = false;
};

zx_status_t MockBlockDevice::FifoTransaction(block_fifo_request_t* requests, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (requests[i].opcode == BLOCKIO_TRIM) {
      saw_trim_ = true;
      return ZX_OK;
    }
  }
  return FakeBlockDevice::FifoTransaction(requests, count);
}

zx_status_t MockBlockDevice::BlockGetInfo(fuchsia_hardware_block_BlockInfo* info) const {
  zx_status_t status = FakeBlockDevice::BlockGetInfo(info);
  if (status == ZX_OK) {
    info->flags |= fuchsia_hardware_block_FLAG_TRIM_SUPPORT;
  }
  return status;
}

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

std::unique_ptr<MockBlockDevice> CreateAndFormatDevice() {
  auto device = std::make_unique<MockBlockDevice>(kNumBlocks, kBlockSize);
  EXPECT_EQ(FormatFilesystem(device.get()), ZX_OK);
  return device;
}

class BlobfsTest : public testing::Test {
 public:
  void SetUp() final {
    MountOptions options;
    std::unique_ptr<MockBlockDevice> device = CreateAndFormatDevice();
    ASSERT_TRUE(device);
    device_ = device.get();
    loop_.StartThread();
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_),
              ZX_OK);
    srand(testing::UnitTest::GetInstance()->random_seed());
  }

 protected:
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  MockBlockDevice* device_ = nullptr;
  std::unique_ptr<Blobfs> fs_;
};

TEST_F(BlobfsTest, GetDevice) { ASSERT_EQ(device_, fs_->GetDevice()); }

TEST_F(BlobfsTest, BlockNumberToDevice) {
  ASSERT_EQ(42 * kBlobfsBlockSize / kBlockSize, fs_->BlockNumberToDevice(42));
}

TEST_F(BlobfsTest, CleanFlag) {
  // Scope all operations while the filesystem is alive to ensure they
  // don't have dangling references once it is destroyed.
  {
    storage::VmoBuffer buffer;
    ASSERT_EQ(buffer.Initialize(fs_.get(), 1, kBlobfsBlockSize, "source"), ZX_OK);

    // Write the superblock with the clean flag unset on Blobfs::Create in Setup.
    storage::Operation operation = {};
    memcpy(buffer.Data(0), &fs_->Info(), sizeof(Superblock));
    operation.type = storage::OperationType::kWrite;
    operation.dev_offset = 0;
    operation.length = 1;

    ASSERT_EQ(fs_->RunOperation(operation, &buffer), ZX_OK);

    // Read the superblock with the clean flag unset.
    operation.type = storage::OperationType::kRead;
    ASSERT_EQ(fs_->RunOperation(operation, &buffer), ZX_OK);
    Superblock* info = reinterpret_cast<Superblock*>(buffer.Data(0));
    EXPECT_EQ(0u, (info->flags & kBlobFlagClean));
  }

  // Destroy the blobfs instance to force writing of the clean bit.
  auto device = Blobfs::Destroy(std::move(fs_));

  // Read the superblock, verify the clean flag is set.
  uint8_t block[kBlobfsBlockSize] = {};
  static_assert(sizeof(block) >= sizeof(Superblock));
  ASSERT_EQ(device->ReadBlock(0, kBlobfsBlockSize, &block), ZX_OK);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  EXPECT_EQ(kBlobFlagClean, (info->flags & kBlobFlagClean));
}

// Tests reading a well known location.
TEST_F(BlobfsTest, RunOperationExpectedRead) {
  storage::VmoBuffer buffer;
  ASSERT_EQ(buffer.Initialize(fs_.get(), 1, kBlobfsBlockSize, "source"), ZX_OK);

  // Read the first block.
  storage::Operation operation = {};
  operation.type = storage::OperationType::kRead;
  operation.length = 1;
  ASSERT_EQ(fs_->RunOperation(operation, &buffer), ZX_OK);

  uint64_t* data = reinterpret_cast<uint64_t*>(buffer.Data(0));
  EXPECT_EQ(kBlobfsMagic0, data[0]);
  EXPECT_EQ(kBlobfsMagic1, data[1]);
}

// Tests that we can read back what we write.
TEST_F(BlobfsTest, RunOperationReadWrite) {
  char data[kBlobfsBlockSize] = "something to test";

  storage::VmoBuffer buffer;
  ASSERT_EQ(buffer.Initialize(fs_.get(), 1, kBlobfsBlockSize, "source"), ZX_OK);
  memcpy(buffer.Data(0), data, kBlobfsBlockSize);

  storage::Operation operation = {};
  operation.type = storage::OperationType::kWrite;
  operation.dev_offset = 1;
  operation.length = 1;

  ASSERT_EQ(fs_->RunOperation(operation, &buffer), ZX_OK);

  memset(buffer.Data(0), 'a', kBlobfsBlockSize);
  operation.type = storage::OperationType::kRead;
  ASSERT_EQ(fs_->RunOperation(operation, &buffer), ZX_OK);

  ASSERT_EQ(memcmp(data, buffer.Data(0), kBlobfsBlockSize), 0);
}

TEST_F(BlobfsTest, TrimsData) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_EQ(fs_->OpenRootNode(&root), ZX_OK);
  fs::Vnode* root_node = root.get();

  std::unique_ptr<BlobInfo> info;
  GenerateRandomBlob("", 1024, &info);
  memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root_node->Create(info->path, 0, &file), ZX_OK);

  size_t actual;
  EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);
  EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &actual), ZX_OK);
  EXPECT_EQ(file->Close(), ZX_OK);

  EXPECT_FALSE(device_->saw_trim());
  ASSERT_EQ(root_node->Unlink(info->path, false), ZX_OK);

  sync_completion_t completion;
  fs_->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
  EXPECT_EQ(sync_completion_wait(&completion, zx::duration::infinite().get()), ZX_OK);

  ASSERT_TRUE(device_->saw_trim());
}

}  // namespace
}  // namespace blobfs
