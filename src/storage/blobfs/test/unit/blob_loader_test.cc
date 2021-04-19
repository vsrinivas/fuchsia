// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blob_loader.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <set>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/lib/digest/digest.h"
#include "src/lib/digest/merkle-tree.h"
#include "src/lib/digest/node-digest.h"
#include "src/lib/storage/vfs/cpp/paged_vfs.h"
#include "src/storage/blobfs/blob.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/compression_settings.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/blobfs_test_setup.h"
#include "src/storage/blobfs/test/unit/utils.h"

namespace blobfs {

namespace {

constexpr uint32_t kTestBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kTestBlockSize;

}  // namespace

using ::testing::Combine;
using ::testing::TestParamInfo;
using ::testing::TestWithParam;
using ::testing::Values;
using ::testing::ValuesIn;

using TestParamType = std::tuple<CompressionAlgorithm, BlobLayoutFormat>;

class BlobLoaderTest : public TestWithParam<TestParamType> {
 public:
  void SetUp() override {
    CompressionAlgorithm compression_algorithm;
    std::tie(compression_algorithm, blob_layout_format_) = GetParam();
    srand(testing::UnitTest::GetInstance()->random_seed());

    FilesystemOptions fs_options{
        .blob_layout_format = blob_layout_format_,
    };
    switch (compression_algorithm) {
      case CompressionAlgorithm::UNCOMPRESSED:
      case CompressionAlgorithm::CHUNKED:
        break;
      case CompressionAlgorithm::ZSTD:
      case CompressionAlgorithm::ZSTD_SEEKABLE:
      case CompressionAlgorithm::LZ4:
        fs_options.oldest_minor_version = kBlobfsMinorVersionBackupSuperblock;
        break;
    }
    options_ = {.compression_settings = {
                    .compression_algorithm = compression_algorithm,
                }};
    ASSERT_EQ(ZX_OK, setup_.CreateFormatMount(kNumBlocks, kTestBlockSize, fs_options, options_));

    // Pre-seed with some random blobs.
    for (unsigned i = 0; i < 3; i++) {
      AddBlob(1024);
    }
    ASSERT_EQ(ZX_OK, setup_.Remount(options_));
  }

  // AddBlob creates and writes a blob of a specified size to the file system.
  // The contents of the blob are compressible at a realistic level for a typical ELF binary.
  // The returned BlobInfo describes the created blob, but its lifetime is unrelated to the lifetime
  // of the on-disk blob.
  [[maybe_unused]] std::unique_ptr<BlobInfo> AddBlob(size_t sz) {
    fbl::RefPtr<fs::Vnode> root;
    EXPECT_EQ(setup_.blobfs()->OpenRootNode(&root), ZX_OK);
    fs::Vnode* root_node = root.get();

    std::unique_ptr<BlobInfo> info = GenerateRealisticBlob("", sz);
    memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

    fbl::RefPtr<fs::Vnode> file;
    EXPECT_EQ(root_node->Create(info->path, 0, &file), ZX_OK);

    size_t actual;
    EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);
    EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &actual), ZX_OK);
    EXPECT_EQ(actual, info->size_data);
    EXPECT_EQ(file->Close(), ZX_OK);

    return info;
  }

  BlobLoader& loader() { return setup_.blobfs()->loader(); }

  CompressionAlgorithm ExpectedAlgorithm() const {
    return options_.compression_settings.compression_algorithm;
  }

  fbl::RefPtr<Blob> LookupBlob(const BlobInfo& info) {
    Digest digest;
    fbl::RefPtr<CacheNode> node;
    EXPECT_EQ(digest.Parse(info.path), ZX_OK);
    EXPECT_EQ(setup_.blobfs()->Cache().Lookup(digest, &node), ZX_OK);
    return fbl::RefPtr<Blob>::Downcast(std::move(node));
  }

  uint32_t LookupInode(const BlobInfo& info) { return LookupBlob(info)->Ino(); }

  zx_status_t LoadBlobData(Blob* blob, std::vector<uint8_t>* data) {
    data->clear();

    fs::VnodeAttributes attrs;
    if (zx_status_t status = blob->GetAttributes(&attrs); status != ZX_OK)
      return status;

    // Always read, even for 0-length blobs, to make sure we test the read path in this case.
    data->resize(attrs.content_size + 1);
    size_t actual = 0xdedbeef;  // Make sure this gets written to.
    if (zx_status_t status = blob->Read(&(*data)[0], data->size(), 0, &actual); status != ZX_OK)
      return status;

    EXPECT_EQ(attrs.content_size, actual);
    data->resize(actual);

    return ZX_OK;
  }

  std::vector<uint8_t> LoadPagedBlobData(Blob* blob) {
    zx::vmo vmo;
    size_t size = 0;
    EXPECT_EQ(ZX_OK, blob->GetVmo(fuchsia_io::wire::kVmoFlagRead, &vmo, &size));
    EXPECT_TRUE(vmo.is_valid());

    // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
    std::vector<uint8_t> result;
    result.resize(size);
    EXPECT_EQ(vmo.read(&result[0], 0, size), ZX_OK);
    return result;
  }

  CompressionAlgorithm LookupCompression(const BlobInfo& info) {
    Digest digest;
    fbl::RefPtr<CacheNode> node;
    EXPECT_EQ(digest.Parse(info.path), ZX_OK);
    EXPECT_EQ(setup_.blobfs()->Cache().Lookup(digest, &node), ZX_OK);
    auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(node));
    auto algorithm_or = AlgorithmForInode(vnode->GetNode());
    EXPECT_TRUE(algorithm_or.is_ok());
    return algorithm_or.value();
  }

  // Used to access protected Blob members because this class is a friend.
  const fzl::OwnedVmoMapper& GetBlobMerkleMapper(const Blob* blob) { return blob->merkle_mapping_; }
  bool IsBlobPagerBacked(const Blob& blob) const {
    std::lock_guard lock(blob.mutex_);
    return blob.IsPagerBacked();
  }

  void CheckMerkleTreeContents(const fzl::OwnedVmoMapper& merkle, const BlobInfo& info) {
    std::unique_ptr<MerkleTreeInfo> merkle_tree = CreateMerkleTree(
        info.data.get(), info.size_data, ShouldUseCompactMerkleTreeFormat(blob_layout_format_));
    ASSERT_TRUE(merkle.vmo().is_valid());
    ASSERT_GE(merkle.size(), merkle_tree->merkle_tree_size);
    switch (blob_layout_format_) {
      case BlobLayoutFormat::kPaddedMerkleTreeAtStart:
        // In the padded layout the Merkle starts at the start of the vmo.
        EXPECT_EQ(
            memcmp(merkle.start(), merkle_tree->merkle_tree.get(), merkle_tree->merkle_tree_size),
            0);
        break;
      case BlobLayoutFormat::kCompactMerkleTreeAtEnd:
        // In the compact layout the Merkle tree is aligned to end at the end of the vmo.
        EXPECT_EQ(memcmp(static_cast<const uint8_t*>(merkle.start()) +
                             (merkle.size() - merkle_tree->merkle_tree_size),
                         merkle_tree->merkle_tree.get(), merkle_tree->merkle_tree_size),
                  0);
        break;
    }
  }

 protected:
  BlobfsTestSetup setup_;

  MountOptions options_;
  BlobLayoutFormat blob_layout_format_;
};

// A separate parameterized test fixture that will only be run with compression algorithms that
// support paging.
using BlobLoaderPagedTest = BlobLoaderTest;

TEST_P(BlobLoaderTest, SmallBlob) {
  size_t blob_len = 1024;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);
  // We explicitly don't check the compression algorithm was respected here, since files this small
  // don't need to be compressed.

  auto result = loader().LoadBlob(LookupInode(*info), nullptr);
  ASSERT_TRUE(result.is_ok());

  ASSERT_TRUE(result->data_vmo.is_valid());
  ASSERT_GE(result->data_mapper.size(), info->size_data);
  EXPECT_EQ(memcmp(result->data_mapper.start(), info->data.get(), info->size_data), 0);

  EXPECT_FALSE(result->merkle.vmo().is_valid());
  EXPECT_EQ(result->merkle.size(), 0ul);
}

TEST_P(BlobLoaderPagedTest, SmallBlob) {
  size_t blob_len = 1024;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);
  // We explicitly don't check the compression algorithm was respected here, since files this small
  // don't need to be compressed.

  auto blob = LookupBlob(*info);
  EXPECT_TRUE(IsBlobPagerBacked(*blob));

  std::vector<uint8_t> data = LoadPagedBlobData(blob.get());
  ASSERT_TRUE(info->DataEquals(&data[0], data.size()));

  // Verify there's no Merkle data for this small blob.
  const auto& merkle = GetBlobMerkleMapper(blob.get());
  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(merkle.size(), 0ul);
}

TEST_P(BlobLoaderTest, LargeBlob) {
  size_t blob_len = 1 << 18;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);
  ASSERT_EQ(LookupCompression(*info), ExpectedAlgorithm());

  auto result = loader().LoadBlob(LookupInode(*info), nullptr);
  ASSERT_TRUE(result.is_ok());

  ASSERT_TRUE(result->data_vmo.is_valid());
  ASSERT_GE(result->data_mapper.size(), info->size_data);
  EXPECT_EQ(memcmp(result->data_mapper.start(), info->data.get(), info->size_data), 0);

  CheckMerkleTreeContents(result->merkle, *info);
}

TEST_P(BlobLoaderTest, LargeBlobWithNonAlignedLength) {
  size_t blob_len = (1 << 18) - 1;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);
  ASSERT_EQ(LookupCompression(*info), ExpectedAlgorithm());

  auto result = loader().LoadBlob(LookupInode(*info), nullptr);
  ASSERT_TRUE(result.is_ok());

  ASSERT_TRUE(result->data_vmo.is_valid());
  ASSERT_GE(result->data_mapper.size(), info->size_data);
  EXPECT_EQ(memcmp(result->data_mapper.start(), info->data.get(), info->size_data), 0);

  CheckMerkleTreeContents(result->merkle, *info);
}

TEST_P(BlobLoaderPagedTest, LargeBlob) {
  size_t blob_len = 1 << 18;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);
  ASSERT_EQ(LookupCompression(*info), ExpectedAlgorithm());

  auto blob = LookupBlob(*info);
  EXPECT_TRUE(IsBlobPagerBacked(*blob));

  std::vector<uint8_t> data = LoadPagedBlobData(blob.get());
  ASSERT_TRUE(info->DataEquals(&data[0], data.size()));

  CheckMerkleTreeContents(GetBlobMerkleMapper(blob.get()), *info);
}

TEST_P(BlobLoaderPagedTest, LargeBlobWithNonAlignedLength) {
  size_t blob_len = (1 << 18) - 1;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);
  ASSERT_EQ(LookupCompression(*info), ExpectedAlgorithm());

  auto blob = LookupBlob(*info);
  EXPECT_TRUE(IsBlobPagerBacked(*blob));

  std::vector<uint8_t> data = LoadPagedBlobData(blob.get());
  ASSERT_TRUE(info->DataEquals(&data[0], data.size()));

  CheckMerkleTreeContents(GetBlobMerkleMapper(blob.get()), *info);
}

TEST_P(BlobLoaderTest, MediumBlobWithRoomForMerkleTree) {
  // In the compact layout the Merkle tree can fit perfectly into the room leftover at the end of
  // the data.
  ASSERT_EQ(setup_.blobfs()->Info().block_size, digest::kDefaultNodeSize);
  size_t blob_len = (digest::kDefaultNodeSize - digest::kSha256Length) * 3;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);

  auto result = loader().LoadBlob(LookupInode(*info), nullptr);
  ASSERT_TRUE(result.is_ok());

  ASSERT_TRUE(result->data_vmo.is_valid());
  ASSERT_GE(result->data_mapper.size(), info->size_data);
  EXPECT_EQ(memcmp(result->data_mapper.start(), info->data.get(), info->size_data), 0);

  CheckMerkleTreeContents(result->merkle, *info);
}

TEST_P(BlobLoaderTest, NullBlobWithCorruptedMerkleRootFailsToLoad) {
  std::unique_ptr<BlobInfo> info = AddBlob(0);

  // The added empty blob should be valid.
  auto blob = LookupBlob(*info);
  ASSERT_EQ(ZX_OK, blob->Verify());

  std::vector<uint8_t> data;
  ASSERT_EQ(ZX_OK, LoadBlobData(blob.get(), &data));

  uint8_t corrupt_merkle_root[digest::kSha256Length] = "-corrupt-null-blob-merkle-root-";
  {
    // Corrupt the null blob's merkle root.
    // |inode| holds a pointer into |blobfs()| and needs to be destroyed before remounting.
    auto inode = setup_.blobfs()->GetNode(blob->Ino());
    memcpy(inode->merkle_root_hash, corrupt_merkle_root, sizeof(corrupt_merkle_root));
    BlobTransaction transaction;
    uint64_t block = (blob->Ino() * kBlobfsInodeSize) / kBlobfsBlockSize;
    transaction.AddOperation(
        {.vmo = zx::unowned_vmo(setup_.blobfs()->GetAllocator()->GetNodeMapVmo().get()),
         .op = {
             .type = storage::OperationType::kWrite,
             .vmo_offset = block,
             .dev_offset = NodeMapStartBlock(setup_.blobfs()->Info()) + block,
             .length = 1,
         }});
    transaction.Commit(*setup_.blobfs()->journal());
  }

  // Remount the filesystem so the node cache will pickup the new name for the blob.
  blob.reset();  // Required for Remount() to succeed.
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);

  // Verify the empty blob can be found by the corrupt name.
  BlobInfo corrupt_info;
  Digest corrupt_digest(corrupt_merkle_root);
  strncpy(corrupt_info.path, corrupt_digest.ToString().c_str(), sizeof(info->path));

  // Loading the data should report corruption.
  auto corrupt_blob = LookupBlob(corrupt_info);
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, LoadBlobData(corrupt_blob.get(), &data));
}

TEST_P(BlobLoaderTest, LoadBlobWithAnInvalidNodeIndexIsAnError) {
  uint32_t invalid_node_index = kMaxNodeId - 1;
  auto result = loader().LoadBlob(invalid_node_index, nullptr);
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), ZX_ERR_INVALID_ARGS);
}

TEST_P(BlobLoaderPagedTest, LoadBlobPagedWithAnInvalidNodeIndexIsAnError) {
  uint32_t invalid_node_index = kMaxNodeId - 1;
  auto result = loader().LoadBlobPaged(invalid_node_index, nullptr);
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), ZX_ERR_INVALID_ARGS);
}

TEST_P(BlobLoaderTest, LoadBlobWithACorruptNextNodeIndexIsAnError) {
  std::unique_ptr<BlobInfo> info = AddBlob(1 << 14);
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);

  // Corrupt the next node index of the inode.
  uint32_t invalid_node_index = kMaxNodeId - 1;
  uint32_t node_index = LookupInode(*info);
  auto inode = setup_.blobfs()->GetAllocator()->GetNode(node_index);
  ASSERT_TRUE(inode.is_ok());
  inode->header.next_node = invalid_node_index;
  inode->extent_count = 2;

  auto result = loader().LoadBlobPaged(node_index, nullptr);
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), ZX_ERR_IO_DATA_INTEGRITY);
}

std::string GetTestParamName(const TestParamInfo<TestParamType>& param) {
  auto [compression_algorithm, blob_layout_format] = param.param;
  return GetBlobLayoutFormatNameForTests(blob_layout_format) +
         GetCompressionAlgorithmName(compression_algorithm);
}

constexpr std::array<CompressionAlgorithm, 4> kCompressionAlgorithms = {
    CompressionAlgorithm::UNCOMPRESSED,
    CompressionAlgorithm::ZSTD,
    CompressionAlgorithm::ZSTD_SEEKABLE,
    CompressionAlgorithm::CHUNKED,
};

constexpr std::array<CompressionAlgorithm, 2> kPagingCompressionAlgorithms = {
    CompressionAlgorithm::UNCOMPRESSED,
    CompressionAlgorithm::CHUNKED,
};

constexpr std::array<BlobLayoutFormat, 2> kBlobLayoutFormats = {
    BlobLayoutFormat::kPaddedMerkleTreeAtStart,
    BlobLayoutFormat::kCompactMerkleTreeAtEnd,
};

INSTANTIATE_TEST_SUITE_P(OldFormat, BlobLoaderTest,
                         Combine(ValuesIn(kCompressionAlgorithms),
                                 Values(BlobLayoutFormat::kPaddedMerkleTreeAtStart)),
                         GetTestParamName);

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BlobLoaderTest,
                         Combine(ValuesIn(kPagingCompressionAlgorithms),
                                 Values(BlobLayoutFormat::kCompactMerkleTreeAtEnd)),
                         GetTestParamName);

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BlobLoaderPagedTest,
                         Combine(ValuesIn(kPagingCompressionAlgorithms),
                                 ValuesIn(kBlobLayoutFormats)),
                         GetTestParamName);

}  // namespace blobfs
