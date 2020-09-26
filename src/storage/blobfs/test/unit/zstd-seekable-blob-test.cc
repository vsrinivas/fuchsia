// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compression/zstd-seekable-blob.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <limits>
#include <memory>

#include <blobfs/common.h>
#include <blobfs/compression-settings.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <fbl/auto_call.h>
#include <gtest/gtest.h>

#include "allocator/allocator.h"
#include "blob.h"
#include "blobfs.h"
#include "compression/zstd-seekable-blob-collection.h"
#include "test/blob_utils.h"

namespace blobfs {
namespace {

using blobfs::BlobInfo;
using blobfs::GenerateBlob;

constexpr uint32_t kNumFilesystemBlocks = 400;
constexpr int kCanaryInt = 0x00AC;
constexpr uint8_t kCanaryByte = 0xAC;
constexpr uint8_t kNotCanaryByte = static_cast<uint8_t>(~kCanaryByte);

void ZeroToSevenBlobSrcFunction(char* data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    uint8_t value = static_cast<uint8_t>(i % 8);
    data[i] = value;
  }
}

void CanaryBlobSrcFunction(char* data, size_t length) { memset(data, kCanaryInt, length); }

class ZSTDSeekableBlobTest : public testing::Test {
 public:
  void SetUp() {
    MountOptions options = {.compression_settings = {
                                .compression_algorithm = CompressionAlgorithm::ZSTD_SEEKABLE,
                            }};
    auto device =
        std::make_unique<block_client::FakeBlockDevice>(kNumFilesystemBlocks, kBlobfsBlockSize);
    ASSERT_EQ(FormatFilesystem(device.get()), ZX_OK);
    loop_.StartThread();

    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_),
              ZX_OK);
    ASSERT_EQ(
        ZSTDSeekableBlobCollection::Create(vmoid_registry(), space_manager(), transaction_handler(),
                                           node_finder(), &compressed_blob_collection_),
        ZX_OK);
  }

  void AddBlobAndSync(std::unique_ptr<BlobInfo>* out_info) {
    AddBlob(out_info);
    ASSERT_EQ(Sync(), ZX_OK);
  }

  void CheckRead(uint32_t node_index, std::vector<uint8_t>* buf, std::vector<uint8_t>* expected_buf,
                 uint64_t data_byte_offset, uint64_t num_bytes) {
    uint8_t* expected = expected_buf->data() + data_byte_offset;
    ASSERT_EQ(
        compressed_blob_collection()->Read(node_index, buf->data(), data_byte_offset, num_bytes),
        ZX_OK);
    ASSERT_EQ(memcmp(expected, buf->data(), num_bytes), 0);
  }

 protected:
  // Use a blob size that is large enough to avoid aborting compression.
  uint64_t blob_size_ = 2 * kCompressionSizeThresholdBytes;

  uint32_t LookupInode(const BlobInfo& info) {
    Digest digest;
    fbl::RefPtr<CacheNode> node;
    EXPECT_EQ(digest.Parse(info.path), ZX_OK);
    EXPECT_EQ(fs_->Cache().Lookup(digest, &node), ZX_OK);
    auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(node));
    return vnode->Ino();
  }

  virtual void AddBlob(std::unique_ptr<BlobInfo>* out_info) {
    AddBlobWithSrcFunction(out_info, ZeroToSevenBlobSrcFunction);
  }

  void AddBlobWithSrcFunction(std::unique_ptr<BlobInfo>* out_info, BlobSrcFunction src_fn) {
    fbl::RefPtr<fs::Vnode> root;
    ASSERT_EQ(fs_->OpenRootNode(&root), ZX_OK);
    fs::Vnode* root_node = root.get();

    std::unique_ptr<BlobInfo> info;

    GenerateBlob(std::move(src_fn), "", blob_size_, &info);
    memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

    fbl::RefPtr<fs::Vnode> file;
    ASSERT_EQ(root_node->Create(info->path, 0, &file), ZX_OK);

    size_t actual;
    EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);
    EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &actual), ZX_OK);
    EXPECT_EQ(actual, info->size_data);
    EXPECT_EQ(file->Close(), ZX_OK);

    if (out_info != nullptr) {
      *out_info = std::move(info);
    }
  }

  zx_status_t Sync() {
    sync_completion_t completion;
    fs_->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
    return sync_completion_wait(&completion, zx::duration::infinite().get());
  }

  SpaceManager* space_manager() { return fs_.get(); }
  virtual NodeFinder* node_finder() { return fs_->GetNodeFinder(); }
  fs::LegacyTransactionHandler* transaction_handler() { return fs_.get(); }
  storage::VmoidRegistry* vmoid_registry() { return fs_.get(); }
  ZSTDSeekableBlobCollection* compressed_blob_collection() {
    return compressed_blob_collection_.get();
  }

  std::unique_ptr<Blobfs> fs_;
  std::unique_ptr<ZSTDSeekableBlobCollection> compressed_blob_collection_;
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

class ZSTDSeekableBlobWrongAlgorithmTest : public ZSTDSeekableBlobTest {
 public:
  void SetUp() {
    MountOptions options = {.compression_settings = {
                                .compression_algorithm = CompressionAlgorithm::ZSTD,
                            }};
    auto device =
        std::make_unique<block_client::FakeBlockDevice>(kNumFilesystemBlocks, kBlobfsBlockSize);
    ASSERT_EQ(FormatFilesystem(device.get()), ZX_OK);
    loop_.StartThread();

    // Construct BlobFS with non-seekable ZSTD algorithm. This should cause errors in the seekable
    // read path.
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_),
              ZX_OK);

    ASSERT_EQ(
        ZSTDSeekableBlobCollection::Create(vmoid_registry(), space_manager(), transaction_handler(),
                                           node_finder(), &compressed_blob_collection_),
        ZX_OK);
  }
};

class ZSTDSeekAndReadTest : public ZSTDSeekableBlobTest {
 public:
  void SetUp() {
    // Write uncompressed to test block device-level seek and read.
    MountOptions options = {.compression_settings = {
                                .compression_algorithm = CompressionAlgorithm::UNCOMPRESSED,
                            }};

    // Use a blob size that will have a "leftover byte" in an extra block to test block read edge
    // case.
    blob_size_ = kBlobfsBlockSize + 1;

    auto device =
        std::make_unique<block_client::FakeBlockDevice>(kNumFilesystemBlocks, kBlobfsBlockSize);
    ASSERT_EQ(FormatFilesystem(device.get()), ZX_OK);
    loop_.StartThread();

    // Construct BlobFS with non-seekable ZSTD algorithm. This should cause errors in the seekable
    // read path.
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_),
              ZX_OK);

    ASSERT_EQ(
        ZSTDSeekableBlobCollection::Create(vmoid_registry(), space_manager(), transaction_handler(),
                                           node_finder(), &compressed_blob_collection_),
        ZX_OK);
  }

  void AddBlob(std::unique_ptr<BlobInfo>* out_info) final {
    AddBlobWithSrcFunction(out_info, CanaryBlobSrcFunction);
  }
};

class NullNodeFinder : public NodeFinder {
 public:
  InodePtr GetNode(uint32_t node_index) final { return {}; }
};

class ZSTDSeekableBlobNullNodeFinderTest : public ZSTDSeekableBlobTest {
 protected:
  NodeFinder* node_finder() final { return &node_finder_; }

  NullNodeFinder node_finder_;
};

// Ensure that a read with size that fits into one block but with data stored in two blocks loads
// data correctly.
TEST_F(ZSTDSeekAndReadTest, SmallReadOverTwoBlocks) {
  std::unique_ptr<BlobInfo> blob_info;
  AddBlobAndSync(&blob_info);

  uint32_t node_index = LookupInode(*blob_info);

  // Use blob size that ensures reading last two bytes will load different blocks.
  const uint64_t blob_data_size = kBlobfsBlockSize + 1;
  ASSERT_EQ(blob_data_size, blob_info->size_data);

  // Perform setup usually managed by `ZSTDSeekableBlobCollection`. This is done manually because
  // the test will manually invoke `ZSTDSeek` and `ZSTDRead` rather than
  // `ZSTDSeekableBlobCollection.Read()` invoking them indirectly.
  const uint64_t read_buffer_num_bytes = fbl::round_up(blob_data_size, kBlobfsBlockSize);
  fzl::OwnedVmoMapper mapper;
  ASSERT_EQ(mapper.CreateAndMap(read_buffer_num_bytes, "zstd-seekable-compressed"), ZX_OK);
  // Note: `fzl::OwnedVmoMapper` would be cleaner than an auto call, but constraints on
  // `ZSTDSeekableBlob::Create()` (which exist due to constraints on its clients) require using the
  // unowned variant here.
  storage::OwnedVmoid vmoid(vmoid_registry());
  ASSERT_EQ(vmoid.AttachVmo(mapper.vmo()), ZX_OK);
  uint32_t num_merkle_blocks = ComputeNumMerkleTreeBlocks(*node_finder()->GetNode(node_index));
  auto blocks = std::make_unique<ZSTDCompressedBlockCollectionImpl>(
      &vmoid, 2 /* 2 blocks in only blob in test */, space_manager(), transaction_handler(),
      node_finder(), node_index, num_merkle_blocks);

  // Extract blocks pointer for use in testing `ZSTDRead` API before moving it.
  auto blocks_for_file = blocks.get();

  std::unique_ptr<ZSTDSeekableBlob> blob;
  ASSERT_EQ(ZSTDSeekableBlob::Create(&mapper, std::move(blocks), &blob), ZX_OK);

  ZSTDSeekableFile file = ZSTDSeekableFile{
      .blob = blob.get(),
      .blocks = blocks_for_file,
      .byte_offset = 0,
      // `ZSTDRead()` attempts to compensate for the fact that the entire blob is a
      // `ZSTDSeekableHeader` followed by an archive. Hence, configure the number of bytes of the
      // archive as `sizeof(entire blob) - sizeof(ZSTDSeekableHeader)`.
      .num_bytes = blob_data_size - sizeof(ZSTDSeekableHeader),
      .status = ZX_OK,
  };

  // Seek to point at last two bytes of blob. These bytes are in different blocks.
  ASSERT_EQ(0, ZSTDSeek(&file, -2, SEEK_END));

  uint8_t expected[2] = {kCanaryByte, kCanaryByte};
  uint8_t buf[2] = {kNotCanaryByte, kNotCanaryByte};
  ASSERT_EQ(0, ZSTDRead(&file, buf, 2));
  ASSERT_EQ(memcmp(expected, buf, 2), 0);
}

TEST_F(ZSTDSeekableBlobTest, CompleteRead) {
  std::unique_ptr<BlobInfo> blob_info;
  AddBlobAndSync(&blob_info);
  uint32_t node_index = LookupInode(*blob_info);
  std::vector<uint8_t> buf(blob_info->size_data);
  std::vector<uint8_t> expected(blob_info->size_data);
  ZeroToSevenBlobSrcFunction(reinterpret_cast<char*>(expected.data()), blob_info->size_data);
  ASSERT_EQ(compressed_blob_collection()->Read(node_index, buf.data(), 0, blob_info->size_data),
            ZX_OK);
  ASSERT_EQ(memcmp(expected.data(), buf.data(), blob_info->size_data), 0);
}

TEST_F(ZSTDSeekableBlobTest, PartialRead) {
  std::unique_ptr<BlobInfo> blob_info;
  AddBlobAndSync(&blob_info);
  uint32_t node_index = LookupInode(*blob_info);
  std::vector<uint8_t> buf(blob_info->size_data);

  // Load whole blob contents (because it's less error-prone). Only some will be used for
  // verification.
  std::vector<uint8_t> expected_buf(blob_info->size_data);
  ZeroToSevenBlobSrcFunction(reinterpret_cast<char*>(expected_buf.data()), blob_info->size_data);

  // Use some small primes to choose "near the end, but not at the end" read of a prime number of
  // bytes.
  uint64_t data_byte_offset = blob_info->size_data - 29;
  uint64_t num_bytes = 19;

  CheckRead(node_index, &buf, &expected_buf, data_byte_offset, num_bytes);
}

TEST_F(ZSTDSeekableBlobTest, MultipleReads) {
  std::unique_ptr<BlobInfo> blob_info;
  AddBlobAndSync(&blob_info);
  uint32_t node_index = LookupInode(*blob_info);
  std::vector<uint8_t> buf(blob_info->size_data);

  // Load whole blob contents (because it's less error-prone). Only some will be used for
  // verification.
  std::vector<uint8_t> expected_buf(blob_info->size_data);
  ZeroToSevenBlobSrcFunction(reinterpret_cast<char*>(expected_buf.data()), blob_info->size_data);

  // Use some small primes to choose "near the end, but not at the end" read of a prime number of
  // bytes.
  {
    uint64_t data_byte_offset = blob_info->size_data - 29;
    uint64_t num_bytes = 19;

    CheckRead(node_index, &buf, &expected_buf, data_byte_offset, num_bytes);
  }
  {
    uint64_t data_byte_offset = blob_info->size_data - 89;
    uint64_t num_bytes = 61;

    CheckRead(node_index, &buf, &expected_buf, data_byte_offset, num_bytes);
  }
  {
    uint64_t data_byte_offset = blob_info->size_data - 53;
    uint64_t num_bytes = 37;

    CheckRead(node_index, &buf, &expected_buf, data_byte_offset, num_bytes);
  }
}

TEST_F(ZSTDSeekableBlobTest, BadOffset) {
  std::unique_ptr<BlobInfo> blob_info;
  AddBlobAndSync(&blob_info);
  uint32_t node_index = LookupInode(*blob_info);

  // Attempt to read one byte passed the end of the blob.
  std::vector<uint8_t> buf(1);
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY,
            compressed_blob_collection()->Read(node_index, buf.data(), blob_info->size_data, 1));
}

TEST_F(ZSTDSeekableBlobTest, BadSize) {
  std::unique_ptr<BlobInfo> blob_info;
  AddBlobAndSync(&blob_info);
  uint32_t node_index = LookupInode(*blob_info);

  // Attempt to read two bytes: the last byte in the blob, and one byte passed the end.
  std::vector<uint8_t> buf(2);
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, compressed_blob_collection()->Read(
                                          node_index, buf.data(), blob_info->size_data - 1, 2));
}

TEST_F(ZSTDSeekableBlobNullNodeFinderTest, BadNode) {
  std::vector<uint8_t> buf(1);

  // Attempt to read a byte from a node that doesn't exist.
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, compressed_blob_collection()->Read(42, buf.data(), 0, 1));
}

TEST_F(ZSTDSeekableBlobWrongAlgorithmTest, BadFlags) {
  std::unique_ptr<BlobInfo> blob_info;
  AddBlobAndSync(&blob_info);
  uint32_t node_index = LookupInode(*blob_info);
  std::vector<uint8_t> buf(1);

  // Attempt to read a byte from a blob that is not zstd-seekable.
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, compressed_blob_collection()->Read(node_index, buf.data(), 0, 1));
}

}  // namespace
}  // namespace blobfs
