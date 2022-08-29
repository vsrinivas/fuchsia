// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/digest/digest.h"
#include "src/lib/digest/node-digest.h"
#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/blobfs/blob.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/compression/blob_compressor.h"
#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression/compressor.h"
#include "src/storage/blobfs/compression/seekable_decompressor.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/fsck.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/blobfs_test_setup.h"
#include "src/storage/blobfs/test/unit/local_decompressor_creator.h"
#include "src/storage/blobfs/test/unit/utils.h"
#include "zircon/compiler.h"

namespace blobfs {

namespace {

constexpr uint32_t kTestDeviceBlockSize = 512;
constexpr uint32_t kTestDeviceNumBlocks = 400 * kBlobfsBlockSize / kTestDeviceBlockSize;
constexpr size_t kTestBlobSize = kBlobfsBlockSize * 20;

// Test cases must write blobs with at least two levels in the Merkle tree to cover all branches.
static_assert(kTestBlobSize > kBlobfsBlockSize);

using OfflineCompressionTestParams = std::tuple<BlobLayoutFormat, /*streaming_writes*/ bool>;

}  // namespace

class OfflineCompressionTest : public BlobfsTestSetup,
                               public testing::TestWithParam<OfflineCompressionTestParams> {
 public:
  void SetUp() override {
    auto device =
        std::make_unique<block_client::FakeBlockDevice>(kTestDeviceNumBlocks, kTestDeviceBlockSize);

    const FilesystemOptions filesystem_options{
        .blob_layout_format = std::get<0>(GetParam()),
    };
    ASSERT_EQ(FormatFilesystem(device.get(), filesystem_options), ZX_OK);

    const MountOptions mount_options{
        .sandbox_decompression = true,
        .streaming_writes = std::get<1>(GetParam()),
        .offline_compression = true,
    };
    ASSERT_EQ(ZX_OK, Mount(std::move(device), mount_options));
  }
};

class OfflineCompressionDisabledTest : public BlobfsTestSetup, public ::testing::Test {
 public:
  void SetUp() override {
    auto device =
        std::make_unique<block_client::FakeBlockDevice>(kTestDeviceNumBlocks, kTestDeviceBlockSize);
    ASSERT_EQ(FormatFilesystem(device.get(), {}), ZX_OK);
    ASSERT_EQ(ZX_OK, Mount(std::move(device), {
                                                  .offline_compression = false,
                                              }));
  }
};

namespace {

TEST_F(OfflineCompressionDisabledTest, CreationFails) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob("", kTestBlobSize);
  auto root = OpenRoot();
  // Remove leading path component.
  const std::string merkle_root = std::string(info->path + 1);
  const std::string compressed_path = merkle_root + blobfs::kChunkedFileExtension;
  fbl::RefPtr<fs::Vnode> file;
  // Ensure creation fails since we mounted Blobfs with offline compression disabled.
  ASSERT_EQ(root->Create(compressed_path, 0, &file), ZX_ERR_NOT_SUPPORTED);
  // Ensure we can create files normally.
  ASSERT_EQ(root->Create(merkle_root, 0, &file), ZX_OK);
}

TEST_P(OfflineCompressionTest, WritePreCompressedBlob) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob("", kTestBlobSize);

  BlobCompressor compressor =
      BlobCompressor::Create({.compression_algorithm = CompressionAlgorithm::kChunked},
                             info->size_data)
          .value();
  ASSERT_EQ(compressor.Update(info->data.get(), info->size_data), ZX_OK);
  ASSERT_EQ(compressor.End(), ZX_OK);

  auto root = OpenRoot();
  // Remove leading path component.
  const std::string merkle_root = std::string(info->path + 1);
  const std::string compressed_path = merkle_root + blobfs::kChunkedFileExtension;

  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Create(compressed_path, 0, &file), ZX_OK);
  ASSERT_EQ(file->Truncate(compressor.Size()), ZX_OK);

  size_t out_actual;
  ASSERT_EQ(file->Write(compressor.Data(), compressor.Size(), 0, &out_actual), ZX_OK);
  ASSERT_EQ(out_actual, compressor.Size());

  ASSERT_EQ(file->Close(), ZX_OK);

  fbl::RefPtr<fs::Vnode> file_ptr;
  ASSERT_EQ(root->Lookup(merkle_root, &file_ptr), ZX_OK);

  ASSERT_EQ(file_ptr->OpenValidating({}, &file), ZX_OK);

  // Validate file contents.
  std::vector<uint8_t> file_contents(kTestBlobSize);
  ASSERT_EQ(file->Read(file_contents.data(), kTestBlobSize, 0, &out_actual), ZX_OK);
  ASSERT_EQ(std::memcmp(info->data.get(), file_contents.data(), kTestBlobSize), 0)
      << "Blob contents don't match after writing to disk.";

  ASSERT_EQ(file->Close(), ZX_OK);
}

std::string GetTestParamName(const ::testing::TestParamInfo<OfflineCompressionTestParams>& param) {
  const auto& [layout, streaming_writes] = param.param;
  return GetBlobLayoutFormatNameForTests(layout) + std::string(streaming_writes ? "Streaming" : "");
}

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, OfflineCompressionTest,
    testing::Combine(testing::Values(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                     BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart),
                     testing::Values(false, true)),
    GetTestParamName);

}  // namespace
}  // namespace blobfs
