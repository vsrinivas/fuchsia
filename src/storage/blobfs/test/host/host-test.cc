// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <memory>

#include <blobfs/blob-layout.h>
#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/host.h>
#include <blobfs/node-finder.h>
#include <digest/digest.h>
#include <digest/node-digest.h>
#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/blobfs-checker.h"

namespace blobfs {
namespace {

class File {
 public:
  explicit File(FILE* file) : file_(file) {}
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  File(File&&) = delete;
  File& operator=(File&&) = delete;

  int fd() const { return fileno(file_); }

  ~File() { fclose(file_); }

 private:
  FILE* file_;
};

std::unique_ptr<Blobfs> CreateBlobfs(uint64_t block_count, FilesystemOptions options) {
  File fs_file(tmpfile());
  if (ftruncate(fs_file.fd(), block_count * kBlobfsBlockSize) == -1) {
    ADD_FAILURE() << "Failed to resize the file for " << block_count << " blocks";
    return nullptr;
  }
  if (Mkfs(fs_file.fd(), block_count, options) == -1) {
    ADD_FAILURE() << "Mkfs failed";
    return nullptr;
  }
  fbl::unique_fd fs_fd(dup(fs_file.fd()));
  std::unique_ptr<Blobfs> blobfs;
  zx_status_t status;
  if ((status = blobfs_create(&blobfs, std::move(fs_fd))) != ZX_OK) {
    ADD_FAILURE() << "blobfs_created returned: " << status;
    return nullptr;
  }
  return blobfs;
}

// Returns the Inode for the last node to be added to |blobfs|.
Inode GetLastCreatedInode(Blobfs& blobfs) {
  // This takes advantage of the fact that the host Blobfs doesn't support creating blobs with
  // extent containers so all allocated nodes in the node map will be inodes.  The host Blobfs
  // also allocates the inodes consecutively.
  int32_t node_index = blobfs.Info().alloc_inode_count - 1;
  InodePtr inode_ptr = blobfs.GetNode(node_index);
  // Double check the above assumptions.
  ZX_ASSERT(inode_ptr->header.IsAllocated());
  ZX_ASSERT(inode_ptr->header.IsInode());
  // The inode_ptr is pointing to memory that gets reused inside of Blobfs so the inode is copied
  // here to avoid lifetime issues.
  return *inode_ptr;
}

// Adds an uncompressed blob of size |data_size| to |blobfs| and returns the created blob's Inode.
Inode AddUncompressedBlob(uint64_t data_size, Blobfs& blobfs) {
  File blob_file(tmpfile());
  EXPECT_EQ(ftruncate(blob_file.fd(), data_size), 0);
  EXPECT_EQ(blobfs_add_blob(&blobfs, /*json_recorder=*/nullptr, blob_file.fd()), ZX_OK);

  return GetLastCreatedInode(blobfs);
}

// Adds a compressed blob with an uncompressed size of |data_size| to |blobfs| and returns the
// created blob's Inode.  The blobs data will be all zeros which will be significantly compressed.
Inode AddCompressedBlob(uint64_t data_size, Blobfs& blobfs) {
  File blob_file(tmpfile());
  EXPECT_EQ(ftruncate(blob_file.fd(), data_size), 0);
  MerkleInfo info;
  EXPECT_EQ(blobfs_preprocess(blob_file.fd(), true, GetBlobLayoutFormat(blobfs.Info()), &info),
            ZX_OK);
  // Make sure that the blob was compressed.
  EXPECT_TRUE(info.compressed);
  EXPECT_EQ(blobfs_add_blob_with_merkle(&blobfs, /*json_recorder=*/nullptr, blob_file.fd(), info),
            ZX_OK);
  return GetLastCreatedInode(blobfs);
}

TEST(BlobfsHostFormatTest, FormatDevice) {
  File file(tmpfile());
  EXPECT_EQ(Mkfs(file.fd(), 10000, FilesystemOptions{}), 0);
}

TEST(BlobfsHostFormatTest, FormatZeroBlockDevice) {
  File file(tmpfile());
  EXPECT_EQ(Mkfs(file.fd(), 0, FilesystemOptions{}), -1);
}

TEST(BlobfsHostFormatTest, FormatTooSmallDevice) {
  File file(tmpfile());
  EXPECT_EQ(Mkfs(file.fd(), 1, FilesystemOptions{}), -1);
}

// This test verifies that formatting actually writes zero-filled
// blocks within the journal.
TEST(BlobfsHostFormatTest, JournalFormattedAsEmpty) {
  File file(tmpfile());
  constexpr uint64_t kBlockCount = 10000;
  EXPECT_EQ(Mkfs(file.fd(), kBlockCount, FilesystemOptions{}), 0);

  char block[kBlobfsBlockSize] = {};
  ASSERT_EQ(ReadBlock(file.fd(), 0, block), ZX_OK);
  static_assert(sizeof(Superblock) <= sizeof(block), "Superblock too big");
  const Superblock* superblock = reinterpret_cast<Superblock*>(block);
  ASSERT_EQ(CheckSuperblock(superblock, kBlockCount), ZX_OK);

  uint64_t journal_blocks = JournalBlocks(*superblock);
  char zero_block[kBlobfsBlockSize] = {};

  // '1' -> Skip the journal info block.
  for (uint64_t n = 1; n < journal_blocks; n++) {
    char block[kBlobfsBlockSize] = {};
    ASSERT_EQ(ReadBlock(file.fd(), JournalStartBlock(*superblock) + n, block), ZX_OK);
    EXPECT_EQ(memcmp(zero_block, block, kBlobfsBlockSize), 0)
        << "Journal should be formatted with zeros";
  }
}

// Verify that we compress small files.
TEST(BlobfsHostCompressionTest, CompressSmallFiles) {
  File fs_file(tmpfile());
  EXPECT_EQ(Mkfs(fs_file.fd(), 10000, FilesystemOptions{}), 0);

  constexpr size_t all_zero_size = 12 * 1024;
  File blob_file(tmpfile());
  EXPECT_EQ(ftruncate(blob_file.fd(), all_zero_size), 0);

  constexpr bool compress = true;
  MerkleInfo info;
  EXPECT_EQ(blobfs_preprocess(blob_file.fd(), compress, BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                              &info),
            ZX_OK);

  EXPECT_TRUE(info.compressed);
  EXPECT_LE(info.compressed_length, all_zero_size);
}

TEST(BlobfsHostTest, WriteBlobWithPaddedFormatIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kPaddedMerkleTreeAtStart});
  ASSERT_TRUE(blobfs != nullptr);

  // In the padded format the Merkle tree can't share a block with the data.
  Inode inode =
      AddUncompressedBlob(blobfs->GetBlockSize() * 2 - digest::kSha256Length * 2, *blobfs);
  EXPECT_FALSE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 3u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Initialize(), ZX_OK);
  EXPECT_EQ(checker.Check(), ZX_OK);
}

TEST(BlobfsHostTest, WriteBlobWithCompactFormatAndSharedBlockIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd});
  ASSERT_TRUE(blobfs != nullptr);

  // In the compact format the Merkle tree will fit perfectly into the end of the data.
  ASSERT_EQ(blobfs->GetBlockSize(), digest::kDefaultNodeSize);
  Inode inode =
      AddUncompressedBlob(blobfs->GetBlockSize() * 2 - digest::kSha256Length * 2, *blobfs);
  EXPECT_FALSE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 2u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Initialize(), ZX_OK);
  EXPECT_EQ(checker.Check(), ZX_OK);
}

TEST(BlobfsHostTest, WriteBlobWithCompactFormatAndBlockIsNotSharedIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd});
  ASSERT_TRUE(blobfs != nullptr);

  // The Merkle tree doesn't fit in with the data.
  ASSERT_EQ(blobfs->GetBlockSize(), digest::kDefaultNodeSize);
  Inode inode = AddUncompressedBlob(blobfs->GetBlockSize() * 2 - 10, *blobfs);
  EXPECT_FALSE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 3u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Initialize(), ZX_OK);
  EXPECT_EQ(checker.Check(), ZX_OK);
}

TEST(BlobfsHostTest, WriteCompressedBlobWithCompactFormatAndSharedBlockIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd});
  ASSERT_TRUE(blobfs != nullptr);

  // The blob is compressed to well under 1 block which leaves plenty of room for the Merkle tree.
  Inode inode = AddCompressedBlob(blobfs->GetBlockSize() * 2, *blobfs);
  EXPECT_TRUE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 1u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Initialize(), ZX_OK);
  EXPECT_EQ(checker.Check(), ZX_OK);
}

TEST(BlobfsHostTest, WriteCompressedBlobWithPaddedFormatIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kPaddedMerkleTreeAtStart});
  ASSERT_TRUE(blobfs != nullptr);

  // The Merkle tree requires 1 block and the blob is compressed to under 1 block.
  Inode inode = AddCompressedBlob(blobfs->GetBlockSize() * 2, *blobfs);
  EXPECT_TRUE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 2u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Initialize(), ZX_OK);
  EXPECT_EQ(checker.Check(), ZX_OK);
}

TEST(BlobfsHostTest, WriteEmptyBlobWithCompactFormatIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd});
  ASSERT_TRUE(blobfs != nullptr);

  Inode inode = AddUncompressedBlob(/*data_size=*/0, *blobfs);
  EXPECT_EQ(inode.block_count, 0u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Initialize(), ZX_OK);
  EXPECT_EQ(checker.Check(), ZX_OK);
}

}  // namespace
}  // namespace blobfs
