// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blob.h"

#include <zircon/assert.h>

#include <condition_variable>

#include <blobfs/blob-layout.h>
#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <digest/digest.h>
#include <digest/node-digest.h>
#include <fbl/auto_call.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/unit/utils.h"

namespace blobfs {
namespace {

constexpr const char kEmptyBlobName[] =
    "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b";

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;
namespace fio = ::llcpp::fuchsia::io;

class BlobTest : public testing::TestWithParam<BlobLayoutFormat> {
 public:
  void SetUp() override {
    auto device = std::make_unique<block_client::FakeBlockDevice>(kNumBlocks, kBlockSize);
    device_ = device.get();
    ASSERT_EQ(FormatFilesystem(device.get(),
                               FilesystemOptions{
                                   .blob_layout_format = GetParam(),
                               }),
              ZX_OK);

    ASSERT_EQ(
        Blobfs::Create(loop_.dispatcher(), std::move(device), MountOptions(), zx::resource(), &fs_),
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

TEST_P(BlobTest, TruncateWouldOverflow) {
  fbl::RefPtr root = OpenRoot();
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Create(kEmptyBlobName, 0, &file), ZX_OK);

  EXPECT_EQ(file->Truncate(UINT64_MAX), ZX_ERR_OUT_OF_RANGE);
}

// Tests that Blob::Sync issues the callback in the right way in the right cases. This does not
// currently test that the data was actually written to the block device.
TEST_P(BlobTest, SyncBehavior) {
  auto root = OpenRoot();

  std::unique_ptr<BlobInfo> info;
  GenerateRandomBlob("", 64, GetBlobLayoutFormat(fs_->Info()), &info);
  memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Create(info->path, 0, &file), ZX_OK);

  size_t out_actual = 0;
  EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);

  // Try syncing before the data has been written. This currently issues an error synchronously but
  // we accept either synchronous or asynchronous callbacks.
  sync_completion_t sync;
  file->Sync([&](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_BAD_STATE, status);
    sync_completion_signal(&sync);
  });
  sync_completion_wait(&sync, ZX_TIME_INFINITE);

  EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &out_actual), ZX_OK);
  EXPECT_EQ(info->size_data, out_actual);

  // It's difficult to get a precise hook into the period between when data has been written and
  // when it has been flushed to disk.  The journal will delay flushing metadata, so the following
  // should test sync being called before metadata has been flushed, and then again afterwards.
  for (int i = 0; i < 2; ++i) {
    sync_completion_t sync;
    file->Sync([&](zx_status_t status) {
      EXPECT_EQ(ZX_OK, status) << i;
      sync_completion_signal(&sync);
    });
    sync_completion_wait(&sync, ZX_TIME_INFINITE);
  }
}

TEST_P(BlobTest, ReadingBlobZerosTail) {
  // Remount without compression so that we can manipulate the data that is loaded.
  MountOptions options = {.compression_settings = {
                              .compression_algorithm = CompressionAlgorithm::UNCOMPRESSED,
                          }};
  ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), Blobfs::Destroy(std::move(fs_)), options,
                           zx::resource(), &fs_),
            ZX_OK);

  std::unique_ptr<BlobInfo> info;
  uint64_t block;
  {
    auto root = OpenRoot();
    GenerateRandomBlob("", 64, GetBlobLayoutFormat(fs_->Info()), &info);
    fbl::RefPtr<fs::Vnode> file;
    ASSERT_EQ(root->Create(info->path + 1, 0, &file), ZX_OK);
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

  // Corrupt the end of the page.
  static_cast<uint8_t*>(buffer.Data(0))[PAGE_SIZE - 1] = 1;

  // Write the block back.
  request.opcode = BLOCKIO_WRITE;
  ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

  // Remount and try and read the blob.
  ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), std::move(device), options, zx::resource(), &fs_),
            ZX_OK);

  auto root = OpenRoot();
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Lookup(info->path + 1, &file), ZX_OK);

  // Trying to read from the blob would fail if the tail wasn't zeroed.
  size_t actual;
  uint8_t data;
  EXPECT_EQ(file->Read(&data, 1, 0, &actual), ZX_OK);
  {
    zx::vmo vmo = {};
    size_t data_size;
    EXPECT_EQ(file->GetVmo(fio::VMO_FLAG_READ, &vmo, &data_size), ZX_OK);
    EXPECT_EQ(data_size, 64ul);

    size_t vmo_size;
    EXPECT_EQ(vmo.get_size(&vmo_size), ZX_OK);
    ASSERT_EQ(vmo_size, size_t{PAGE_SIZE});

    uint8_t data;
    EXPECT_EQ(vmo.read(&data, PAGE_SIZE - 1, 1), ZX_OK);
    // The corrupted bit in the tail was zeroed when being read.
    EXPECT_EQ(data, 0);
  }
}

TEST_P(BlobTest, ReadWriteAllCompressionFormats) {
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
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), Blobfs::Destroy(std::move(fs_)), options,
                             zx::resource(), &fs_),
              ZX_OK);

    auto root = OpenRoot();
    std::unique_ptr<BlobInfo> info;

    // Write the blob
    {
      GenerateRealisticBlob("", 1 << 16, GetBlobLayoutFormat(fs_->Info()), &info);
      fbl::RefPtr<fs::Vnode> file;
      ASSERT_EQ(root->Create(info->path + 1, 0, &file), ZX_OK);
      size_t out_actual = 0;
      EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);
      EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &out_actual), ZX_OK);
      EXPECT_EQ(out_actual, info->size_data);
    }

    // Remount with same compression algorithm.
    // This prevents us from relying on caching when we read back the blob.
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), Blobfs::Destroy(std::move(fs_)), options,
                             zx::resource(), &fs_),
              ZX_OK);
    root = OpenRoot();

    // Read back the blob
    {
      fbl::RefPtr<fs::Vnode> file;
      ASSERT_EQ(root->Lookup(info->path + 1, &file), ZX_OK);
      size_t actual;
      uint8_t data[info->size_data];
      EXPECT_EQ(file->Read(&data, info->size_data, 0, &actual), ZX_OK);
      EXPECT_EQ(info->size_data, actual);
      EXPECT_EQ(memcmp(data, info->data.get(), info->size_data), 0);
    }
  }
}

TEST_P(BlobTest, WriteBlobWithSharedBlockInCompactFormat) {
  // Remount without compression so we can force a specific blob size in storage.
  MountOptions options = {.compression_settings = {
                              .compression_algorithm = CompressionAlgorithm::UNCOMPRESSED,
                          }};
  ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), Blobfs::Destroy(std::move(fs_)), options,
                           zx::resource(), &fs_),
            ZX_OK);

  std::unique_ptr<BlobInfo> info;
  {
    // Create a blob where the Merkle tree in the compact layout fits perfectly into the space
    // remaining at the end of the blob.
    ASSERT_EQ(fs_->Info().block_size, digest::kDefaultNodeSize);
    GenerateRealisticBlob("", (digest::kDefaultNodeSize - digest::kSha256Length) * 3,
                          GetBlobLayoutFormat(fs_->Info()), &info);
    if (GetBlobLayoutFormat(fs_->Info()) == BlobLayoutFormat::kCompactMerkleTreeAtEnd) {
      EXPECT_EQ(info->size_data + info->size_merkle, digest::kDefaultNodeSize * 3);
    }
    fbl::RefPtr<fs::Vnode> file;
    auto root = OpenRoot();
    ASSERT_EQ(root->Create(info->path + 1, 0, &file), ZX_OK);
    size_t out_actual = 0;
    EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);
    EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &out_actual), ZX_OK);
    EXPECT_EQ(out_actual, info->size_data);
  }

  // Remount to avoid caching.
  ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), Blobfs::Destroy(std::move(fs_)), options,
                           zx::resource(), &fs_),
            ZX_OK);

  // Read back the blob
  {
    fbl::RefPtr<fs::Vnode> file;
    auto root = OpenRoot();
    ASSERT_EQ(root->Lookup(info->path + 1, &file), ZX_OK);
    size_t actual;
    uint8_t data[info->size_data];
    EXPECT_EQ(file->Read(&data, info->size_data, 0, &actual), ZX_OK);
    EXPECT_EQ(info->size_data, actual);
    EXPECT_EQ(memcmp(data, info->data.get(), info->size_data), 0);
  }
}

TEST_P(BlobTest, WriteErrorsAreFused) {
  std::unique_ptr<BlobInfo> info;
  GenerateRandomBlob("", kBlockSize * kNumBlocks, GetBlobLayoutFormat(fs_->Info()), &info);
  auto root = OpenRoot();
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Create(info->path + 1, 0, &file), ZX_OK);
  ASSERT_EQ(file->Truncate(info->size_data), ZX_OK);
  uint64_t out_actual;
  EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &out_actual), ZX_ERR_NO_SPACE);
  // Writing just 1 byte now should see the same error returned.
  EXPECT_EQ(file->Write(info->data.get(), 1, 0, &out_actual), ZX_ERR_NO_SPACE);
}

std::string GetTestParamName(const ::testing::TestParamInfo<BlobLayoutFormat>& param) {
  return GetBlobLayoutFormatNameForTests(param.param);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BlobTest,
                         ::testing::Values(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                           BlobLayoutFormat::kCompactMerkleTreeAtEnd),
                         GetTestParamName);

}  // namespace
}  // namespace blobfs
