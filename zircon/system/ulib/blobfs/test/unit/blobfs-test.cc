// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs.h"

#include <lib/sync/completion.h>

#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <fs-test-utils/blobfs/blobfs.h>
#include <storage/buffer/vmo-buffer.h>
#include <zxtest/zxtest.h>

#include "directory.h"
#include "utils.h"

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
    std::unique_ptr<MockBlockDevice> device = CreateAndFormatDevice();
    ASSERT_TRUE(device);
    device_ = device.get();
    ASSERT_OK(Blobfs::Create(std::move(device), &options, &fs_));
  }

 protected:
  MockBlockDevice* device_ = nullptr;
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

TEST_F(BlobfsTest, TrimsData) {
  fbl::RefPtr<Directory> root;
  ASSERT_OK(fs_->OpenRootNode(&root));
  fs::Vnode* root_node = root.get();

  std::unique_ptr<fs_test_utils::BlobInfo> info;
  ASSERT_TRUE(fs_test_utils::GenerateRandomBlob("", 1024, &info));
  memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

  fbl::RefPtr<fs::Vnode> file;
  ASSERT_OK(root_node->Create(&file, info->path, 0));

  size_t actual;
  EXPECT_OK(file->Truncate(info->size_data));
  EXPECT_OK(file->Write(info->data.get(), info->size_data, 0, &actual));
  EXPECT_OK(file->Close());

  EXPECT_FALSE(device_->saw_trim());
  ASSERT_OK(root_node->Unlink(info->path, false));

  zx_status_t sync_result;
  sync_completion_t completion;
  fs_->Sync([&sync_result, &completion](zx_status_t status) {
    sync_completion_signal(&completion);
    sync_result = status;
  });
  EXPECT_OK(sync_completion_wait(&completion, zx::duration::infinite().get()));
  EXPECT_OK(sync_result);

  ASSERT_TRUE(device_->saw_trim());
}

}  // namespace
}  // namespace blobfs
