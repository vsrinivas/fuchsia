// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blob_loader.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/result.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <set>

#include <gtest/gtest.h>

#include "src/lib/digest/digest.h"
#include "src/lib/digest/merkle-tree.h"
#include "src/lib/digest/node-digest.h"
#include "src/lib/storage/block_client/cpp/fake_block_device.h"
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
#include "src/storage/blobfs/test/test_scoped_vnode_open.h"
#include "src/storage/blobfs/test/unit/local_decompressor_creator.h"
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
    auto connector_or = LocalDecompressorCreator::Create();
    ASSERT_TRUE(connector_or.is_ok());
    decompressor_creator_ = std::move(connector_or.value());
    options_ = {
        .compression_settings = {.compression_algorithm = compression_algorithm},
        .sandbox_decompression = true,
        .decompression_connector = &decompressor_creator_->GetDecompressorConnector(),
    };
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
    EXPECT_EQ(setup_.blobfs()->GetCache().Lookup(digest, &node), ZX_OK);
    return fbl::RefPtr<Blob>::Downcast(std::move(node));
  }

  uint32_t LookupInode(const BlobInfo& info) { return LookupBlob(info)->Ino(); }

  zx_status_t LoadBlobData(Blob* blob, std::vector<uint8_t>& data) {
    TestScopedVnodeOpen opener(blob);  // Blob must be open to get the vmo.

    zx::vmo vmo;
    if (zx_status_t status = blob->GetVmo(fuchsia_io::wire::VmoFlags::kRead, &vmo); status != ZX_OK)
      return status;
    EXPECT_TRUE(vmo.is_valid());  // Always expect a valid blob on success.

    // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
    uint64_t size;
    if (zx_status_t status = vmo.get_prop_content_size(&size); status != ZX_OK) {
      return status;
    }
    data.resize(size);
    if (zx_status_t status = vmo.read(data.data(), 0, size); status != ZX_OK) {
      data.resize(0);
      return status;
    }
    return ZX_OK;
  }

  std::vector<uint8_t> LoadBlobData(Blob* blob) {
    std::vector<uint8_t> result;
    EXPECT_EQ(ZX_OK, LoadBlobData(blob, result));
    return result;
  }

  CompressionAlgorithm LookupCompression(const BlobInfo& info) {
    Digest digest;
    fbl::RefPtr<CacheNode> node;
    EXPECT_EQ(digest.Parse(info.path), ZX_OK);
    EXPECT_EQ(setup_.blobfs()->GetCache().Lookup(digest, &node), ZX_OK);
    auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(node));
    auto algorithm_or = AlgorithmForInode(*setup_.blobfs()->GetNode(vnode->Ino()).value());
    EXPECT_TRUE(algorithm_or.is_ok());
    return algorithm_or.value();
  }

  // Used to access protected Blob/BlobVerifier members because this class is a friend.
  cpp20::span<const uint8_t> GetBlobMerkleData(const Blob* blob) const {
    std::lock_guard lock(blob->mutex_);
    return blob->loader_info_.verifier->merkle_data();
  }

  void CheckMerkleTreeContents(cpp20::span<const uint8_t> merkle_data, const BlobInfo& info) {
    std::unique_ptr<MerkleTreeInfo> merkle_tree = CreateMerkleTree(
        info.data.get(), info.size_data, ShouldUseCompactMerkleTreeFormat(blob_layout_format_));
    ASSERT_EQ(merkle_data.size(), merkle_tree->merkle_tree_size);
    EXPECT_EQ(
        memcmp(merkle_data.begin(), merkle_tree->merkle_tree.get(), merkle_tree->merkle_tree_size),
        0);
  }

 protected:
  std::unique_ptr<LocalDecompressorCreator> decompressor_creator_;
  BlobfsTestSetup setup_;

  MountOptions options_;
  BlobLayoutFormat blob_layout_format_;
};

TEST_P(BlobLoaderTest, SmallBlob) {
  size_t blob_len = 1024;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);
  // We explicitly don't check the compression algorithm was respected here, since files this small
  // don't need to be compressed.

  auto blob = LookupBlob(*info);

  std::vector<uint8_t> data = LoadBlobData(blob.get());
  ASSERT_TRUE(info->DataEquals(data.data(), data.size()));

  // Verify there's no Merkle data for this small blob.
  const auto& merkle = GetBlobMerkleData(blob.get());
  EXPECT_EQ(merkle.size(), 0ul);
}

TEST_P(BlobLoaderTest, LargeBlob) {
  size_t blob_len = 1 << 18;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);
  ASSERT_EQ(LookupCompression(*info), ExpectedAlgorithm());

  auto blob = LookupBlob(*info);

  std::vector<uint8_t> data = LoadBlobData(blob.get());
  ASSERT_TRUE(info->DataEquals(data.data(), data.size()));

  CheckMerkleTreeContents(GetBlobMerkleData(blob.get()), *info);
}

TEST_P(BlobLoaderTest, LargeBlobWithNonAlignedLength) {
  size_t blob_len = (1 << 18) - 1;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);
  ASSERT_EQ(LookupCompression(*info), ExpectedAlgorithm());

  auto blob = LookupBlob(*info);

  std::vector<uint8_t> data = LoadBlobData(blob.get());
  ASSERT_TRUE(info->DataEquals(data.data(), data.size()));

  CheckMerkleTreeContents(GetBlobMerkleData(blob.get()), *info);
}

TEST_P(BlobLoaderTest, NullBlobWithCorruptedMerkleRootFailsToLoad) {
  std::unique_ptr<BlobInfo> info = AddBlob(0);

  // The added empty blob should be valid.
  auto blob = LookupBlob(*info);
  {
    TestScopedVnodeOpen open(blob);  // Blob must be open to verify.
    ASSERT_EQ(ZX_OK, blob->Verify());
  }

  uint8_t corrupt_merkle_root[digest::kSha256Length] = "-corrupt-null-blob-merkle-root-";
  {
    // Corrupt the null blob's merkle root.
    // |inode| holds a pointer into |blobfs()| and needs to be destroyed before remounting.
    auto inode = setup_.blobfs()->GetNode(blob->Ino());
    memcpy(inode->merkle_root_hash, corrupt_merkle_root, sizeof(corrupt_merkle_root));
    BlobTransaction transaction;
    const uint64_t block = (blob->Ino() * kBlobfsInodeSize) / kBlobfsBlockSize;
    transaction.AddOperation(
        {.vmo = zx::unowned_vmo(setup_.blobfs()->GetAllocator()->GetNodeMapVmo().get()),
         .op = {
             .type = storage::OperationType::kWrite,
             .vmo_offset = block,
             .dev_offset = NodeMapStartBlock(setup_.blobfs()->Info()) + block,
             .length = 1,
         }});
    transaction.Commit(*setup_.blobfs()->GetJournal());
  }

  // Remount the filesystem so the node cache will pickup the new name for the blob.
  blob.reset();  // Required for Remount() to succeed.
  ASSERT_EQ(setup_.Remount(options_), ZX_OK);

  // Verify the empty blob can be found by the corrupt name.
  BlobInfo corrupt_info;
  Digest corrupt_digest(corrupt_merkle_root);
  strncpy(corrupt_info.path, corrupt_digest.ToString().c_str(), sizeof(info->path));

  // Loading the data should report corruption. This can't use LoadBlobData() because that reads via
  // a VMO which doesn't work for 0-length blobs.
  auto corrupt_blob = LookupBlob(corrupt_info);
  TestScopedVnodeOpen open(corrupt_blob);
  char data_buf;
  size_t num_read = 0;
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, corrupt_blob->Read(&data_buf, 0, 0, &num_read));
}

TEST_P(BlobLoaderTest, LoadBlobWithAnInvalidNodeIndexIsAnError) {
  uint32_t invalid_node_index = kMaxNodeId - 1;
  auto result = loader().LoadBlob(invalid_node_index, nullptr);
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

  auto result = loader().LoadBlob(node_index, nullptr);
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value(), ZX_ERR_IO_DATA_INTEGRITY);
}

std::string GetTestParamName(const TestParamInfo<TestParamType>& param) {
  auto [compression_algorithm, blob_layout_format] = param.param;
  return GetBlobLayoutFormatNameForTests(blob_layout_format) +
         GetCompressionAlgorithmName(compression_algorithm);
}

constexpr std::array<CompressionAlgorithm, 2> kCompressionAlgorithms = {
    CompressionAlgorithm::kUncompressed,
    CompressionAlgorithm::kChunked,
};

constexpr std::array<CompressionAlgorithm, 2> kPagingCompressionAlgorithms = {
    CompressionAlgorithm::kUncompressed,
    CompressionAlgorithm::kChunked,
};

INSTANTIATE_TEST_SUITE_P(OldFormat, BlobLoaderTest,
                         Combine(ValuesIn(kCompressionAlgorithms),
                                 Values(BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart)),
                         GetTestParamName);

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BlobLoaderTest,
                         Combine(ValuesIn(kPagingCompressionAlgorithms),
                                 Values(BlobLayoutFormat::kCompactMerkleTreeAtEnd)),
                         GetTestParamName);

}  // namespace blobfs
