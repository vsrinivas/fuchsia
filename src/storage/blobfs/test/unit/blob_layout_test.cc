// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/storage/blobfs/blob_layout.h"

#include <zircon/errors.h>
#include <zircon/status.h>

#include <limits>
#include <ostream>
#include <sstream>
#include <type_traits>

#include <gtest/gtest.h>

#include "src/lib/digest/digest.h"
#include "src/lib/digest/node-digest.h"
#include "src/storage/blobfs/format.h"

namespace blobfs {
namespace {

constexpr uint64_t kBlockSize = kBlobfsBlockSize;
constexpr size_t kHashSize = digest::kSha256Length;
constexpr size_t kNodeSize = digest::kDefaultNodeSize;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Inode CreateInode(uint64_t file_size, uint64_t block_count) {
  Inode inode = {};
  inode.blob_size = file_size;
  using InodeBlockCountType = decltype(inode.block_count);
  static_assert(std::is_same_v<InodeBlockCountType, uint32_t>,
                "Type of block count in Inode has changed, verify conversion below.");
  inode.block_count = safemath::checked_cast<InodeBlockCountType>(block_count);
  return inode;
}

Inode CreateCompressedInode(uint64_t file_size, uint64_t block_count) {
  Inode inode = CreateInode(file_size, block_count);
  inode.header.flags = kBlobFlagChunkCompressed;
  return inode;
}

TEST(BlobLayoutTest, FileSizeIsCorrect) {
  constexpr uint64_t kFileSize = 10ul * kBlockSize + 200;
  constexpr uint64_t kDataSize = 6ul * kBlockSize + 25;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), kFileSize);
}

TEST(BlobLayoutTest, FileBlockAlignedSizeWithEmptyFileReturnsZero) {
  constexpr uint64_t kFileSize = 0;
  constexpr uint64_t kDataSize = 0;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileBlockAlignedSize(), 0ul);
}

TEST(BlobLayoutTest, FileBlockAlignedSizeWithAlignedFileSizeReturnsFileSize) {
  constexpr uint64_t kFileSize = 10ul * kBlockSize;
  constexpr uint64_t kDataSize = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileBlockAlignedSize(), kFileSize);
}

TEST(BlobLayoutTest, FileBlockAlignedSizeWithUnalignedFileSizeReturnsNextBlockMultiple) {
  constexpr uint64_t kFileSize = 10ul * kBlockSize + 500;
  constexpr uint64_t kDataSize = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileBlockAlignedSize(), 11ul * kBlockSize);
}

TEST(BlobLayoutTest, DataSizeUpperBoundIsCorrect) {
  constexpr uint64_t kFileSize = 10ul * kBlockSize + 200;
  constexpr uint64_t kDataSize = 6ul * kBlockSize + 25;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), kDataSize);
}

TEST(BlobLayoutTest, DataBlockAlignedSizeWithNoDataReturnsZero) {
  constexpr uint64_t kFileSize = 0;
  constexpr uint64_t kDataSize = 0;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockAlignedSize(), 0ul);
}

TEST(BlobLayoutTest, DataBlockAlignedSizeWithAlignedDataReturnsDataSizeUpperBound) {
  constexpr uint64_t kFileSize = 8ul * kBlockSize + 30;
  constexpr uint64_t kDataSize = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockAlignedSize(), kDataSize);
}

TEST(BlobLayoutTest, DataBlockAlignedSizeWithUnalignedDataReturnsNextBlockMultiple) {
  constexpr uint64_t kFileSize = 8ul * kBlockSize + 30;
  constexpr uint64_t kDataSize = 5ul * kBlockSize + 20;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockAlignedSize(), 6ul * kBlockSize);
}

TEST(BlobLayoutTest, DataBlockCountWithNoDataReturnsZero) {
  constexpr uint64_t kFileSize = 0;
  constexpr uint64_t kDataSize = 0;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockCount(), 0u);
}

TEST(BlobLayoutTest, DataBlockCountWithBlockAlignedDataIsCorrect) {
  constexpr uint64_t kFileSize = 500ul * kBlockSize;
  constexpr uint64_t kDataSize = 255ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockCount(), 255u);
}

TEST(BlobLayoutTest, DataBlockCountWithUnalignedDataIsCorrect) {
  constexpr uint64_t kFileSize = 500ul * kBlockSize;
  constexpr uint64_t kDataSize = 255ul * kBlockSize + 90;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockCount(), 256u);
}

TEST(BlobLayoutTest, DataBlockOffsetWithPaddedFormatAndNoMerkleTreeReturnsZero) {
  constexpr uint64_t kFileSize = 100;
  constexpr uint64_t kDataSize = 50;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockOffset(), 0u);
}

TEST(BlobLayoutTest, DataBlockOffsetWithPaddedFormatReturnsEndOfMerkleTree) {
  constexpr uint64_t kFileSize = 600ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockOffset(), 4u);
}

TEST(BlobLayoutTest, DataBlockOffsetWithCompactFormatReturnsZero) {
  constexpr uint64_t kFileSize = 100ul * kBlockSize;
  constexpr uint64_t kDataSize = 100ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockOffset(), 0u);
}

TEST(BlobLayoutTest, MerkleTreeSizeWithPaddedFormatIsCorrect) {
  constexpr uint64_t kFileSize = 600ul * kBlockSize;
  constexpr uint64_t kDataSize = 50ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeSize(), 4ul * kBlockSize);
}

TEST(BlobLayoutTest, MerkleTreeSizeWithCompactFormatIsCorrect) {
  constexpr uint64_t kFileSize = 600ul * kBlockSize;
  constexpr uint64_t kDataSize = 50ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // A file size of 600 blocks produces 600 hashes in the first row of the Merkle tree and 3 hashes
  // in the second row.
  EXPECT_EQ(blob_layout->MerkleTreeSize(), (600ul + 3) * kHashSize);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithNoMerkleTreeReturnsZero) {
  constexpr uint64_t kFileSize = 400ul;
  constexpr uint64_t kDataSize = 200ul;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockAlignedSize(), 0ul);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithPaddedFormatAndAlignedMerkleTreeIsCorrect) {
  // In the padded format the Merkle tree is always a multiple of the node size so making the block
  // size the same as the node size will always produce a block aligned Merkle tree.
  constexpr uint64_t kFileSize = 600ul * kNodeSize;
  constexpr uint64_t kDataSize = 200ul * kNodeSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kNodeSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockAlignedSize(), 4ul * kNodeSize);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithPaddedFormatAndUnalignedMerkleTreeIsCorrect) {
  // The Merkle tree will contain 3 nodes and the block size is twice the node size so the block
  // aligned Merkle tree is 2 blocks.
  constexpr uint64_t kFileSize = 400ul * kNodeSize;
  constexpr uint64_t kDataSize = 200ul * kNodeSize;
  uint64_t block_size = kNodeSize * 2;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, block_size);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockAlignedSize(), 2ul * block_size);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithCompactFormatAndAlignedMerkleTreeIsCorrect) {
  constexpr uint64_t kFileSize = 256ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockAlignedSize(), 1ul * kBlockSize);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithCompactFormatAndUnalignedMerkleTreeIsCorrect) {
  constexpr uint64_t kFileSize = 600ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockAlignedSize(), 3ul * kBlockSize);
}

TEST(BlobLayoutTest, MerkleTreeBlockCountWithNoMerkleTreeReturnsZero) {
  constexpr uint64_t kFileSize = kBlockSize;
  constexpr uint64_t kDataSize = 300ul;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockCount(), 0u);
}

TEST(BlobLayoutTest, MerkleTreeBlockCountWithBlockAlignedMerkleTreeIsCorrect) {
  constexpr uint64_t kFileSize = 700ul * kBlockSize;
  constexpr uint64_t kDataSize = 255ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockCount(), 4u);
}

TEST(BlobLayoutTest, MerkleTreeBlockCountWithUnalignedMerkleTreeIsCorrect) {
  constexpr uint64_t kFileSize = 600ul * kBlockSize;
  constexpr uint64_t kDataSize = 255ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockCount(), 3u);
}

TEST(BlobLayoutTest, MerkleTreeBlockOffsetWithPaddedFormatReturnsZero) {
  constexpr uint64_t kFileSize = 700ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockOffset(), 0ul);
}

TEST(BlobLayoutTest,
     MerkleTreeBlockOffsetWithCompactFormatAndNotSharingABlockReturnsDataBlockCount) {
  constexpr uint64_t kFileSize = 700ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockOffset(), 200u);
}

TEST(BlobLayoutTest, MerkleTreeBlockOffsetWithCompactFormatAndSharingABlockIsCorrect) {
  constexpr uint64_t kFileSize = 700ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize + 1;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockOffset(), 200u);
}

TEST(BlobLayoutTest, MerkleTreeOffsetWithinBlockOffsetWithPaddedFormatReturnsZero) {
  constexpr uint64_t kFileSize = 700ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeOffsetWithinBlockOffset(), 0ul);
}

TEST(BlobLayoutTest,
     MerkleTreeOffsetWithinBlockOffsetWithCompactFormatAndUnalignedMerkleTreeIsCorrect) {
  constexpr uint64_t kFileSize = 700ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // The Merkle tree will contain 700 hashes in the first row which is 2 blocks + 188 hashes.
  // The second row of the Merkle tree will contain 3 hashes.  The Merkle tree will start (188 + 3)
  // hashes in from the end of the 3rd last block.
  EXPECT_EQ(blob_layout->MerkleTreeOffsetWithinBlockOffset(), kBlockSize - 191 * kHashSize);
}

TEST(BlobLayoutTest,
     MerkleTreeOffsetWithinBlockOffsetWithCompactFormatAndBlockAlignedMerkleTreeIsCorrect) {
  constexpr uint64_t kFileSize = 256ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // The Merkle tree requires exactly 1 block so it starts at the start of the last block.
  EXPECT_EQ(blob_layout->MerkleTreeOffsetWithinBlockOffset(), 0ul);
}

TEST(BlobLayoutTest, MerkleTreeOffsetWithLargeFileSize) {
  constexpr uint64_t kFileSize = 1ul << 34;
  constexpr uint64_t kDataSize = 1ul << 33;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  size_t expected_tree_offset =
      blob_layout->TotalBlockCount() * size_t{kBlockSize} - blob_layout->MerkleTreeSize();
  EXPECT_EQ(blob_layout->MerkleTreeOffset(), expected_tree_offset);
}

TEST(BlobLayoutTest, TotalBlockCountWithPaddedFormatIsCorrect) {
  // The Merkle tree requires 4 blocks and the data requires 200.
  constexpr uint64_t kFileSize = 700ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->TotalBlockCount(), 200u + 4u);
}

TEST(BlobLayoutTest, TotalBlockCountWithCompactFormatAndSharedBlockIsCorrect) {
  // The Merkle tree uses 2 blocks + 6016 bytes and the data uses 200 blocks + 10 bytes.
  constexpr uint64_t kFileSize = 700ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize + 10;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // 200 data blocks + 2 Merkle blocks + 1 shared block.
  EXPECT_EQ(blob_layout->TotalBlockCount(), 203u);
}

TEST(BlobLayoutTest, TotalBlockCountWithCompactFormatAndNonSharedBlockIsCorrect) {
  // The Merkle tree uses 2 blocks + 6016 bytes and the data uses 200 blocks.
  constexpr uint64_t kFileSize = 700ul * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // 200 data blocks + 3 Merkle blocks.
  EXPECT_EQ(blob_layout->TotalBlockCount(), 203u);
}

TEST(BlobLayoutTest, HasMerkleTreeAndDataSharedBlockWithPaddedFormatReturnsFalse) {
  constexpr uint64_t kFileSize = 4ul * kBlockSize;
  constexpr uint64_t kDataSize = 4ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->HasMerkleTreeAndDataSharedBlock(), false);
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndBlockAlignedDataReturnsFalse) {
  constexpr uint64_t kFileSize = 4ul * kBlockSize;
  constexpr uint64_t kDataSize = 3ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndBlockAlignedMerkleTreeReturnsFalse) {
  constexpr uint64_t kFileSize = 256ul * kBlockSize;
  constexpr uint64_t kDataSize = 10ul * kBlockSize + 1;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest, HasMerkleTreeAndDataSharedBlockWithCompactFormatAndNoMerkleTreeReturnsFalse) {
  constexpr uint64_t kFileSize = kBlockSize;
  constexpr uint64_t kDataSize = 10ul;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndMerkleTreeDoesNotFitInDataReturnsFalse) {
  constexpr uint64_t kFileSize = 4ul * kBlockSize;
  constexpr uint64_t kDataSize = 3ul * kBlockSize + (kBlockSize - 4 * kHashSize + 1);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // The data takes 3 blocks + 8065 bytes and the Merkle tree takes 128 bytes.
  // 8065 + 128 = 8193 > 8192 So the Merkle tree and data will not share a block.
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndMerkleTreeFitsInDataReturnsTrue) {
  constexpr uint64_t kFileSize = 4ul * kBlockSize;
  constexpr uint64_t kDataSize = 3ul * kBlockSize + (kBlockSize - 4 * kHashSize);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // The data takes 3 blocks + 8064 bytes and the Merkle tree takes 128 bytes.
  // 8064 + 128 = 8192 So the Merkle tree and data will share a block.
  EXPECT_TRUE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndMerkleTreeRemainderFitsInDataReturnsTrue) {
  constexpr uint64_t kFileSize = (256ul + 4) * kBlockSize;
  constexpr uint64_t kDataSize = 200ul * kBlockSize + (kBlockSize - (6 * kHashSize) - 1);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // The data takes 200 blocks + 7999 bytes and the Merkle tree takes 1 block + 192 bytes.
  // 799 + 192 = 8191 < 8192 So the Merkle tree and data will share a block.
  EXPECT_TRUE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest, FormatWithPaddedFormatIsCorrect) {
  constexpr uint64_t kFileSize = 0;
  constexpr uint64_t kDataSize = 0;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->Format(), BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart);
}

TEST(BlobLayoutTest, FormatWithCompactFormatIsCorrect) {
  constexpr uint64_t kFileSize = 0;
  constexpr uint64_t kDataSize = 0;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->Format(), BlobLayoutFormat::kCompactMerkleTreeAtEnd);
}

TEST(BlobLayoutTest, CreateFromInodeWithPaddedFormatAndUncompressedInodeIsCorrect) {
  // 21 blocks of data and 1 block for the Merkle tree.
  constexpr uint64_t kFileSize = 20ul * kBlockSize + 50;
  constexpr uint64_t kBlockCount = 22;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart,
                                  CreateInode(kFileSize, kBlockCount), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), kFileSize);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), kFileSize);
  EXPECT_EQ(blob_layout->TotalBlockCount(), 22u);
}

TEST(BlobLayoutTest, CreateFromInodeWithPaddedFormatAndCompressedInodeIsCorrect) {
  // The Merkle tree takes 1 block leaving 19 for the compressed data.
  constexpr uint64_t kFileSize = 26ul * kBlockSize + 50;
  constexpr uint64_t kBlockCount = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart,
                                  CreateCompressedInode(kFileSize, kBlockCount), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), kFileSize);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), 19ul * kBlockSize);
  EXPECT_EQ(blob_layout->TotalBlockCount(), 20u);
}

TEST(BlobLayoutTest,
     CreateFromInodeWithPaddedFormatAndCompressedInodeAndTooLargeOfMerkleTreeIsError) {
  // The Merkle tree requires more than 2^32 blocks.
  constexpr uint64_t kFileSize = (1ul << 50) * kBlockSize;
  constexpr uint64_t kBlockCount = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart,
                                  CreateCompressedInode(kFileSize, kBlockCount), kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest,
     CreateFromInodeWithCompactFormatAndUncompressedInodeAndNotSharedBlockIsCorrect) {
  constexpr uint64_t kFileSize = 21ul * kBlockSize;
  constexpr uint64_t kBlockCount = 22;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 CreateInode(kFileSize, kBlockCount), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), kFileSize);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), kFileSize);
  EXPECT_EQ(blob_layout->TotalBlockCount(), 22u);
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest, CreateFromInodeWithCompactFormatAndUncompressedInodeAndSharedBlockIsCorrect) {
  constexpr uint64_t kFileSize = 20ul * kBlockSize + 50;
  constexpr uint64_t kBlockCount = 21;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 CreateInode(kFileSize, kBlockCount), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), kFileSize);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), kFileSize);
  EXPECT_EQ(blob_layout->TotalBlockCount(), 21u);
  EXPECT_TRUE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest, CreateFromInodeWithCompactFormatAndCompressedInodeAndSharedBlockIsCorrect) {
  constexpr uint64_t kFileSize = 26ul * kBlockSize + 50;
  constexpr uint64_t kBlockCount = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                  CreateCompressedInode(kFileSize, kBlockCount), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), kFileSize);
  // The file size rounded up is 27 blocks so the Merkle tree will contain 27 hashes.
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), (20ul * kBlockSize) - (27 * kHashSize));
  EXPECT_EQ(blob_layout->TotalBlockCount(), 20u);

  // CreateFromInode on a compressed Inode will always share a block unless the Merkle tree size is
  // a block multiple.
  EXPECT_TRUE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest, CreateFromInodeWithCompactFormatAndCompressedInodeAndNotSharedBlockIsCorrect) {
  // The Merkle tree takes exactly 1 block.
  constexpr uint64_t kFileSize = 256ul * kBlockSize;
  constexpr uint64_t kBlockCount = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                  CreateCompressedInode(kFileSize, kBlockCount), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), kFileSize);
  // The Merkle tree takes exactly 1 block leaving 19 for the data.
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), 19ul * kBlockSize);
  EXPECT_EQ(blob_layout->TotalBlockCount(), 20u);

  // Since the Merkle tree is a block multiple it couldn't have shared a block with the data.
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest, CreateFromSizesWithPaddedFormatAndAndTooLargeOfFileSizeIsError) {
  constexpr uint64_t kFileSize = std::numeric_limits<uint64_t>::max();
  constexpr uint64_t kDataSize = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithCompactFormatAndAndTooLargeOfFileSizeIsError) {
  constexpr uint64_t kFileSize = std::numeric_limits<uint64_t>::max();
  constexpr uint64_t kDataSize = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithPaddedFormatAndAndTooLargeOfDataSizeIsError) {
  constexpr uint64_t kFileSize = (1ul << 35) * kBlockSize;
  constexpr uint64_t kDataSize = (1ul << 34) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithCompactFormatAndAndTooLargeOfDataSizeIsError) {
  constexpr uint64_t kFileSize = (1ul << 35) * kBlockSize;
  constexpr uint64_t kDataSize = (1ul << 34) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithPaddedFormatAndAndTooLargeOfMerkleTreeSizeIsError) {
  constexpr uint64_t kFileSize = ((1ul << 32) + 1) * kBlockSize * 256;
  constexpr uint64_t kDataSize = (1ul << 30);
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithCompactFormatAndTooLargeOfMerkleTreeIsError) {
  constexpr uint64_t kFileSize = ((1ul << 32) + 1) * kBlockSize * 256;
  constexpr uint64_t kDataSize = (1ul << 30);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithPaddedFormatAndTooLargeOfTotalBlockCountIsError) {
  constexpr uint64_t kFileSize = kBlockSize * 2;
  constexpr uint64_t kDataSize = ((1ull << 32) - 1) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(
      BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart, kFileSize, kDataSize, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithCompactFormatAndTooLargeOfTotalBlockCountIsError) {
  constexpr uint64_t kFileSize = kBlockSize * 2;
  constexpr uint64_t kDataSize = ((1ul << 32) - 1) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 kFileSize, kDataSize, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest,
     CreateFromInodeWithPaddedFormatAndUncompressedAndBlockCountDoesNotMatchIsError) {
  constexpr uint64_t kFileSize = 26ul * kBlockSize;
  constexpr uint64_t kBlockCount = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart,
                                  CreateInode(kFileSize, kBlockCount), kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest,
     CreateFromInodeWithCompactFormatAndUncompressedAndBlockCountDoesNotMatchIsError) {
  constexpr uint64_t kFileSize = 26ul * kBlockSize;
  constexpr uint64_t kBlockCount = 20;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 CreateInode(kFileSize, kBlockCount), kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(
    BlobLayoutTest,
    CreateFromInodeWithPaddedFormatAndCompressedAndMerkleTreeBlockCountIsLargerThanBlockCountIsError) {
  constexpr uint64_t kFileSize = 513ul * kBlockSize;
  constexpr uint64_t kBlockCount = 2;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart,
                                  CreateInode(kFileSize, kBlockCount), kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest,
     CreateFromInodeWithCompactFormatAndCompressedAndMerkleTreeSizeIsLargerThanStoredBytesIsError) {
  constexpr uint64_t kFileSize = 513ul * kBlockSize;
  constexpr uint64_t kBlockCount = 2;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 CreateInode(kFileSize, kBlockCount), kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

}  // namespace
}  // namespace blobfs
