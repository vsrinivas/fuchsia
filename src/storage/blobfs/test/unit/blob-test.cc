// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob.h"

#include <condition_variable>

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <fbl/auto_call.h>
#include <gtest/gtest.h>

#include "blobfs.h"
#include "test/blob_utils.h"
#include "utils.h"

namespace blobfs {
namespace {

constexpr const char kEmptyBlobName[] =
    "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b";

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

class BlobTest : public testing::Test {
 public:
  void SetUp() override {
    auto device = std::make_unique<block_client::FakeBlockDevice>(kNumBlocks, kBlockSize);
    device_ = device.get();
    ASSERT_EQ(FormatFilesystem(device.get()), ZX_OK);

    MountOptions options;
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_),
              ZX_OK);
  }

  void TearDown() override { device_ = nullptr; }

  fbl::RefPtr<fs::Vnode> OpenRoot() const {
    fbl::RefPtr<fs::Vnode> root;
    EXPECT_EQ(fs_->OpenRootNode(&root), ZX_OK);
    return root;
  }

 protected:
  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};

  block_client::FakeBlockDevice* device_;
  std::unique_ptr<Blobfs> fs_;
};

TEST_F(BlobTest, Truncate_WouldOverflow) {
  fbl::RefPtr root = OpenRoot();
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Create(&file, kEmptyBlobName, 0), ZX_OK);

  EXPECT_EQ(file->Truncate(UINT64_MAX), ZX_ERR_OUT_OF_RANGE);
}

// Tests that Blob::Sync issues the callback in the right way in the right cases. This does not
// currently test that the data was actually written to the block device.
TEST_F(BlobTest, SyncBehavior) {
  auto root = OpenRoot();

  std::unique_ptr<BlobInfo> info;
  GenerateRandomBlob("", 64, &info);
  memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Create(&file, info->path, 0), ZX_OK);

  size_t out_actual = 0;
  EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);

  // PHASE 1: Incomplete data.
  //
  // Try syncing before the data has been written. This currently issues an error synchronously but
  // we accept either synchronous or asynchronous callbacks.
  file->Sync([loop = &loop_](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_BAD_STATE, status);
    loop->Quit();
  });
  loop_.Run();

  // PHASE 2: Complete data, not yet synced.
  device_->Pause();  // Don't let it sync yet.
  EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &out_actual), ZX_OK);
  EXPECT_EQ(info->size_data, out_actual);

  loop_.ResetQuit();
  file->Sync([loop = &loop_](zx_status_t status) {
    EXPECT_EQ(ZX_OK, status);
    loop->Quit();
  });

  // Allow the Sync to continue and wait for the reply. The system may issue this callback
  // asynchronously. RunUntilIdle can't be used because the backend posts work to another thread and
  // then back here.
  device_->Resume();
  loop_.Run();

  // PHASE 3: Data previously synced.
  //
  // Once the blob is in a fully synced state, calling Sync on it will complete with success.
  loop_.ResetQuit();
  file->Sync([loop = &loop_](zx_status_t status) {
    EXPECT_EQ(ZX_OK, status);
    loop->Quit();
  });
}

TEST_F(BlobTest, ReadingBlobVerifiesTail) {
  // Remount without compression so that we can manipulate the data that is loaded.
  MountOptions options = {.compression_settings = {
                              .compression_algorithm = CompressionAlgorithm::UNCOMPRESSED,
                          }};
  ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), Blobfs::Destroy(std::move(fs_)), &options,
                           zx::resource(), &fs_),
            ZX_OK);

  std::unique_ptr<BlobInfo> info;
  uint64_t block;
  {
    auto root = OpenRoot();
    GenerateRandomBlob("", 64, &info);
    fbl::RefPtr<fs::Vnode> file;
    ASSERT_EQ(root->Create(&file, info->path + 1, 0), ZX_OK);
    size_t out_actual = 0;
    EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);
    EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &out_actual), ZX_OK);
    EXPECT_EQ(out_actual, info->size_data);
    {
      auto blob = fbl::RefPtr<Blob>::Downcast(file);
      block = fs_->GetNode(blob->Ino())->extents[0].Start() + DataStartBlock(fs_->Info());
    }
  }

  // Unmount.
  std::unique_ptr<block_client::BlockDevice> device = Blobfs::Destroy(std::move(fs_));

  // Read the block that contains the blob.
  storage::VmoBuffer buffer;
  ASSERT_EQ(buffer.Initialize(device.get(), 1, kBlobfsBlockSize, "test_buffer"), ZX_OK);
  block_fifo_request_t request = {
      .opcode = BLOCKIO_READ,
      .vmoid = buffer.vmoid(),
      .length = kBlobfsBlockSize / kBlockSize,
      .vmo_offset = 0,
      .dev_offset = block * kBlobfsBlockSize / kBlockSize,
  };
  ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

  // Corrupt the end of the block.
  static_cast<uint8_t*>(buffer.Data(0))[kBlobfsBlockSize - 1] = 1;

  // Write the block back.
  request.opcode = BLOCKIO_WRITE;
  ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

  // Remount and try and read the blob.
  ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_),
            ZX_OK);

  auto root = OpenRoot();
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Lookup(&file, info->path + 1), ZX_OK);

  // Trying to read from the blob should fail with an error.
  size_t actual;
  uint8_t data;
  EXPECT_EQ(file->Read(&data, 1, 0, &actual), ZX_ERR_IO_DATA_INTEGRITY);
}

TEST_F(BlobTest, ReadWriteAllCompressionFormats) {
  CompressionAlgorithm algorithms[] = {
      CompressionAlgorithm::UNCOMPRESSED, CompressionAlgorithm::LZ4,
      CompressionAlgorithm::ZSTD,         CompressionAlgorithm::ZSTD_SEEKABLE,
      CompressionAlgorithm::CHUNKED,
  };

  for (auto algorithm : algorithms) {
    MountOptions options = {.compression_settings = {
                                .compression_algorithm = algorithm,
                            }};

    // Remount with new compression algorithm
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), Blobfs::Destroy(std::move(fs_)), &options,
                             zx::resource(), &fs_),
              ZX_OK);

    auto root = OpenRoot();
    std::unique_ptr<BlobInfo> info;

    // Write the blob
    {
      GenerateRealisticBlob("", 1 << 16, &info);
      fbl::RefPtr<fs::Vnode> file;
      ASSERT_EQ(root->Create(&file, info->path + 1, 0), ZX_OK);
      size_t out_actual = 0;
      EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);
      EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &out_actual), ZX_OK);
      EXPECT_EQ(out_actual, info->size_data);
    }

    // Remount with same compression algorithm.
    // This prevents us from relying on caching when we read back the blob.
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), Blobfs::Destroy(std::move(fs_)), &options,
                             zx::resource(), &fs_),
              ZX_OK);
    root = OpenRoot();

    // Read back the blob
    {
      fbl::RefPtr<fs::Vnode> file;
      ASSERT_EQ(root->Lookup(&file, info->path + 1), ZX_OK);
      size_t actual;
      uint8_t data[info->size_data];
      EXPECT_EQ(file->Read(&data, info->size_data, 0, &actual), ZX_OK);
      EXPECT_EQ(info->size_data, actual);
      EXPECT_EQ(memcmp(data, info->data.get(), info->size_data), 0);
    }
  }
}

}  // namespace
}  // namespace blobfs
