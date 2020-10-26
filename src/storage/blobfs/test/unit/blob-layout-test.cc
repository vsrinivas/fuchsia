// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <zircon/errors.h>
#include <zircon/status.h>

#include <limits>
#include <ostream>
#include <sstream>

#include <blobfs/blob-layout.h>
#include <blobfs/format.h>
#include <digest/digest.h>
#include <digest/node-digest.h>
#include <gtest/gtest.h>

namespace blobfs {
namespace {

using ByteCountType = BlobLayout::ByteCountType;
using BlockCountType = BlobLayout::BlockCountType;
using BlockSizeType = BlobLayout::BlockSizeType;

constexpr BlockSizeType kBlockSize = kBlobfsBlockSize;
constexpr size_t kHashSize = digest::kSha256Length;
constexpr size_t kNodeSize = digest::kDefaultNodeSize;

Inode CreateInode(ByteCountType file_size, BlockCountType block_count) {
  Inode inode = {};
  inode.blob_size = file_size;
  inode.block_count = block_count;
  return inode;
}

Inode CreateCompressedInode(ByteCountType file_size, BlockCountType block_count) {
  Inode inode = CreateInode(file_size, block_count);
  inode.header.flags = kBlobFlagZSTDCompressed;
  return inode;
}

TEST(BlobLayoutTest, FileSizeIsCorrect) {
  ByteCountType file_size = 10ul * kBlockSize + 200;
  ByteCountType data_size = 6ul * kBlockSize + 25;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), file_size);
}

TEST(BlobLayoutTest, FileBlockAlignedSizeWithEmptyFileReturnsZero) {
  ByteCountType file_size = 0;
  ByteCountType data_size = 0;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto file_block_aligned_size = blob_layout->FileBlockAlignedSize();
  ASSERT_TRUE(file_block_aligned_size.is_ok());
  EXPECT_EQ(file_block_aligned_size.value(), 0ul);
}

TEST(BlobLayoutTest, FileBlockAlignedSizeWithAlignedFileSizeReturnsFileSize) {
  ByteCountType file_size = 10ul * kBlockSize;
  ByteCountType data_size = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto file_block_aligned_size = blob_layout->FileBlockAlignedSize();
  ASSERT_TRUE(file_block_aligned_size.is_ok());
  EXPECT_EQ(file_block_aligned_size.value(), file_size);
}

TEST(BlobLayoutTest, FileBlockAlignedSizeWithUnalignedFileSizeReturnsNextBlockMultiple) {
  ByteCountType file_size = 10ul * kBlockSize + 500;
  ByteCountType data_size = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto file_block_aligned_size = blob_layout->FileBlockAlignedSize();
  ASSERT_TRUE(file_block_aligned_size.is_ok());
  EXPECT_EQ(file_block_aligned_size.value(), 11ul * kBlockSize);
}

TEST(BlobLayoutTest, FileBlockAlignedSizeWithTooBigFileSizeIsError) {
  ByteCountType file_size = std::numeric_limits<ByteCountType>::max();
  ByteCountType data_size = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto file_block_aligned_size = blob_layout->FileBlockAlignedSize();
  EXPECT_TRUE(file_block_aligned_size.is_error());
}

TEST(BlobLayoutTest, DataSizeUpperBoundIsCorrect) {
  ByteCountType file_size = 10ul * kBlockSize + 200;
  ByteCountType data_size = 6ul * kBlockSize + 25;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), data_size);
}

TEST(BlobLayoutTest, DataBlockAlignedSizeWithNoDataReturnsZero) {
  ByteCountType file_size = 0;
  ByteCountType data_size = 0;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_block_aligned_size = blob_layout->DataBlockAlignedSize();
  ASSERT_TRUE(data_block_aligned_size.is_ok());
  EXPECT_EQ(data_block_aligned_size.value(), 0ul);
}

TEST(BlobLayoutTest, DataBlockAlignedSizeWithAlignedDataReturnsDataSizeUpperBound) {
  ByteCountType file_size = 8ul * kBlockSize + 30;
  ByteCountType data_size = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_block_aligned_size = blob_layout->DataBlockAlignedSize();
  ASSERT_TRUE(data_block_aligned_size.is_ok());
  EXPECT_EQ(data_block_aligned_size.value(), data_size);
}

TEST(BlobLayoutTest, DataBlockAlignedSizeWithUnalignedDataReturnsNextBlockMultiple) {
  ByteCountType file_size = 8ul * kBlockSize + 30;
  ByteCountType data_size = 5ul * kBlockSize + 20;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_block_aligned_size = blob_layout->DataBlockAlignedSize();
  ASSERT_TRUE(data_block_aligned_size.is_ok());
  EXPECT_EQ(data_block_aligned_size.value(), 6ul * kBlockSize);
}

TEST(BlobLayoutTest, DataBlockAlignedSizeWithTooLargeOfDataSizeIsError) {
  ByteCountType file_size = std::numeric_limits<ByteCountType>::max() - 15;
  ByteCountType data_size = std::numeric_limits<ByteCountType>::max() - 20;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_block_aligned_size = blob_layout->DataBlockAlignedSize();
  EXPECT_TRUE(data_block_aligned_size.is_error());
}

TEST(BlobLayoutTest, DataBlockCountWithNoDataReturnsZero) {
  ByteCountType file_size = 0;
  ByteCountType data_size = 0;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_block_count = blob_layout->DataBlockCount();
  ASSERT_TRUE(data_block_count.is_ok());
  EXPECT_EQ(data_block_count.value(), 0u);
}

TEST(BlobLayoutTest, DataBlockCountWithBlockAlignedDataIsCorrect) {
  ByteCountType file_size = 500ul * kBlockSize;
  ByteCountType data_size = 255ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_block_count = blob_layout->DataBlockCount();
  ASSERT_TRUE(data_block_count.is_ok());
  EXPECT_EQ(data_block_count.value(), 255u);
}

TEST(BlobLayoutTest, DataBlockCountWithUnalignedDataIsCorrect) {
  ByteCountType file_size = 500ul * kBlockSize;
  ByteCountType data_size = 255ul * kBlockSize + 90;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_block_count = blob_layout->DataBlockCount();
  ASSERT_TRUE(data_block_count.is_ok());
  EXPECT_EQ(data_block_count.value(), 256u);
}

TEST(BlobLayoutTest, DataBlockCountWithTooLargeOfDataIsError) {
  ByteCountType file_size = (1ul << 35) * kBlockSize;
  ByteCountType data_size = (1ul << 34) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_block_count = blob_layout->DataBlockCount();
  EXPECT_TRUE(data_block_count.is_error());
}

TEST(BlobLayoutTest, DataBlockOffsetWithPaddedFormatAndNoMerkleTreeReturnsZero) {
  ByteCountType file_size = 100;
  ByteCountType data_size = 50;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_starting_block = blob_layout->DataBlockOffset();
  ASSERT_TRUE(data_starting_block.is_ok());
  EXPECT_EQ(data_starting_block.value(), 0u);
}

TEST(BlobLayoutTest, DataBlockOffsetWithPaddedFormatReturnsEndOfMerkleTree) {
  ByteCountType file_size = 600ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_starting_block = blob_layout->DataBlockOffset();
  ASSERT_TRUE(data_starting_block.is_ok());
  EXPECT_EQ(data_starting_block.value(), 4u);
}

TEST(BlobLayoutTest, DataBlockOffsetWithPaddedFormatAndTooLargeOfMerkleTreeIsError) {
  ByteCountType file_size = (1ul << 41) * kBlockSize;
  ByteCountType data_size = (1ul << 20) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_starting_block = blob_layout->DataBlockOffset();
  EXPECT_TRUE(data_starting_block.is_error());
}

TEST(BlobLayoutTest, DataBlockOffsetWithCompactFormatReturnsZero) {
  ByteCountType file_size = 100ul * kBlockSize;
  ByteCountType data_size = 100ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto data_starting_block = blob_layout->DataBlockOffset();
  ASSERT_TRUE(data_starting_block.is_ok());
  EXPECT_EQ(data_starting_block.value(), 0u);
}

TEST(BlobLayoutTest, MerkleTreeSizeWithPaddedFormatIsCorrect) {
  ByteCountType file_size = 600ul * kBlockSize;
  ByteCountType data_size = 50ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeSize(), 4ul * kBlockSize);
}

TEST(BlobLayoutTest, MerkleTreeSizeWithCompactFormatIsCorrect) {
  ByteCountType file_size = 600ul * kBlockSize;
  ByteCountType data_size = 50ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // A file size of 600 blocks produces 600 hashes in the first row of the Merkle tree and 3 hashes
  // in the second row.
  EXPECT_EQ(blob_layout->MerkleTreeSize(), (600ul + 3) * kHashSize);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithNoMerkleTreeReturnsZero) {
  ByteCountType file_size = 400ul;
  ByteCountType data_size = 200ul;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_block_aligned_size = blob_layout->MerkleTreeBlockAlignedSize();
  ASSERT_TRUE(merkle_tree_block_aligned_size.is_ok());
  EXPECT_EQ(merkle_tree_block_aligned_size.value(), 0ul);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithPaddedFormatAndAlignedMerkleTreeIsCorrect) {
  // In the padded format the Merkle tree is always a multiple of the node size so making the block
  // size the same as the node size will always produce a block aligned Merkle tree.
  ByteCountType file_size = 600ul * kNodeSize;
  ByteCountType data_size = 200ul * kNodeSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kNodeSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_block_aligned_size = blob_layout->MerkleTreeBlockAlignedSize();
  ASSERT_TRUE(merkle_tree_block_aligned_size.is_ok());
  EXPECT_EQ(merkle_tree_block_aligned_size.value(), 4ul * kNodeSize);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithPaddedFormatAndUnalignedMerkleTreeIsCorrect) {
  // The Merkle tree will contain 3 nodes and the block size is twice the node size so the block
  // aligned Merkle tree is 2 blocks.
  ByteCountType file_size = 400ul * kNodeSize;
  ByteCountType data_size = 200ul * kNodeSize;
  ByteCountType block_size = kNodeSize * 2;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, block_size);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_block_aligned_size = blob_layout->MerkleTreeBlockAlignedSize();
  ASSERT_TRUE(merkle_tree_block_aligned_size.is_ok());
  EXPECT_EQ(merkle_tree_block_aligned_size.value(), 2ul * block_size);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithCompactFormatAndAlignedMerkleTreeIsCorrect) {
  ByteCountType file_size = 256ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_block_aligned_size = blob_layout->MerkleTreeBlockAlignedSize();
  ASSERT_TRUE(merkle_tree_block_aligned_size.is_ok());
  EXPECT_EQ(merkle_tree_block_aligned_size.value(), 1ul * kBlockSize);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithCompactFormatAndUnalignedMerkleTreeIsCorrect) {
  ByteCountType file_size = 600ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_block_aligned_size = blob_layout->MerkleTreeBlockAlignedSize();
  ASSERT_TRUE(merkle_tree_block_aligned_size.is_ok());
  EXPECT_EQ(merkle_tree_block_aligned_size.value(), 3ul * kBlockSize);
}

TEST(BlobLayoutTest, MerkleTreeBlockCountWithNoMerkleTreeReturnsZero) {
  ByteCountType file_size = kBlockSize;
  ByteCountType data_size = 300ul;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_block_count = blob_layout->MerkleTreeBlockCount();
  ASSERT_TRUE(merkle_tree_block_count.is_ok());
  EXPECT_EQ(merkle_tree_block_count.value(), 0u);
}

TEST(BlobLayoutTest, MerkleTreeBlockCountWithBlockAlignedMerkleTreeIsCorrect) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 255ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_block_count = blob_layout->MerkleTreeBlockCount();
  ASSERT_TRUE(merkle_tree_block_count.is_ok());
  EXPECT_EQ(merkle_tree_block_count.value(), 4u);
}

TEST(BlobLayoutTest, MerkleTreeBlockCountWithUnalignedMerkleTreeIsCorrect) {
  ByteCountType file_size = 600ul * kBlockSize;
  ByteCountType data_size = 255ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_block_count = blob_layout->MerkleTreeBlockCount();
  ASSERT_TRUE(merkle_tree_block_count.is_ok());
  EXPECT_EQ(merkle_tree_block_count.value(), 3u);
}

TEST(BlobLayoutTest, MerkleTreeBlockCountWithTooLargeOfMerkleTreeIsError) {
  ByteCountType file_size = (1ul << 46) * kBlockSize;
  ByteCountType data_size = (1ul << 34) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_block_count = blob_layout->MerkleTreeBlockCount();
  EXPECT_TRUE(merkle_tree_block_count.is_error());
}

TEST(BlobLayoutTest, MerkleTreeBlockOffsetWithPaddedFormatReturnsZero) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_starting_block = blob_layout->MerkleTreeBlockOffset();
  ASSERT_TRUE(merkle_tree_starting_block.is_ok());
  EXPECT_EQ(merkle_tree_starting_block.value(), 0ul);
}

TEST(BlobLayoutTest,
     MerkleTreeBlockOffsetWithCompactFormatAndNotSharingABlockReturnsDataBlockCount) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_starting_block = blob_layout->MerkleTreeBlockOffset();
  ASSERT_TRUE(merkle_tree_starting_block.is_ok());
  EXPECT_EQ(merkle_tree_starting_block.value(), 200u);
}

TEST(BlobLayoutTest, MerkleTreeBlockOffsetWithCompactFormatAndSharingABlockIsCorrect) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize + 1;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_starting_block = blob_layout->MerkleTreeBlockOffset();
  ASSERT_TRUE(merkle_tree_starting_block.is_ok());
  EXPECT_EQ(merkle_tree_starting_block.value(), 200u);
}

TEST(BlobLayoutTest, MerkleTreeBlockOffsetWithCompactFormatAndTooLargeOfDataIsError) {
  // This calculation requires the total block count which is larger than 2^32.
  ByteCountType file_size = (1ul << 50) * kBlockSize;
  ByteCountType data_size = (1ul << 33) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_starting_block = blob_layout->MerkleTreeBlockOffset();
  EXPECT_TRUE(merkle_tree_starting_block.is_error());
}

TEST(BlobLayoutTest, MerkleTreeOffsetWithinBlockOffsetWithPaddedFormatReturnsZero) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeOffsetWithinBlockOffset(), 0ul);
}

TEST(BlobLayoutTest,
     MerkleTreeOffsetWithinBlockOffsetWithCompactFormatAndUnalignedMerkleTreeIsCorrect) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // The Merkle tree will contain 700 hashes in the first row which is 2 blocks + 188 hashes.
  // The second row of the Merkle tree will contain 3 hashes.  The Merkle tree will start (188 + 3)
  // hashes in from the end of the 3rd last block.
  EXPECT_EQ(blob_layout->MerkleTreeOffsetWithinBlockOffset(), kBlockSize - 191 * kHashSize);
}

TEST(BlobLayoutTest,
     MerkleTreeOffsetWithinBlockOffsetWithCompactFormatAndBlockAlignedMerkleTreeIsCorrect) {
  ByteCountType file_size = 256ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // The Merkle tree requires exactly 1 block so it starts at the start of the last block.
  EXPECT_EQ(blob_layout->MerkleTreeOffsetWithinBlockOffset(), 0ul);
}

TEST(BlobLayoutTest, TotalBlockCountWithPaddedFormatIsCorrect) {
  // The Merkle tree requires 4 blocks and the data requires 200.
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto total_block_count = blob_layout->TotalBlockCount();
  ASSERT_TRUE(total_block_count.is_ok());
  EXPECT_EQ(total_block_count.value(), 200u + 4u);
}

TEST(BlobLayoutTest, TotalBlockCountWithPaddedFormatAndTooLargeOfDataIsError) {
  // The data requires more than 2^32 blocks.
  ByteCountType file_size = (1ul << 34) * kBlockSize;
  ByteCountType data_size = (1ul << 33) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto total_block_count = blob_layout->TotalBlockCount();
  EXPECT_TRUE(total_block_count.is_error());
  // Verify it was the data that caused the error.
  EXPECT_TRUE(blob_layout->DataBlockCount().is_error());
  EXPECT_TRUE(blob_layout->MerkleTreeBlockCount().is_ok());
}

TEST(BlobLayoutTest, TotalBlockCountWithPaddedFormatAndTooLargeOfMerkleTreeIsError) {
  // The Merkle tree requires more than 2^32 blocks.
  ByteCountType file_size = (1ul << 50) * kBlockSize;
  ByteCountType data_size = (1ul << 31) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto total_block_count = blob_layout->TotalBlockCount();
  EXPECT_TRUE(total_block_count.is_error());
  // Verify it was the Merkle tree that caused the error.
  EXPECT_TRUE(blob_layout->DataBlockCount().is_ok());
  EXPECT_TRUE(blob_layout->MerkleTreeBlockCount().is_error());
}

TEST(BlobLayoutTest, TotalBlockCountWithPaddedFormatRequiringTooManyBlocksIsError) {
  // The Merkle tree and data both require less than 2^32 blocks but their sum is larger.
  ByteCountType file_size = (1ul << 39) * kBlockSize;
  ByteCountType data_size = (1ul << 31) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto total_block_count = blob_layout->TotalBlockCount();
  EXPECT_TRUE(total_block_count.is_error());
  // Verify that neither individually caused the error.
  EXPECT_TRUE(blob_layout->DataBlockCount().is_ok());
  EXPECT_TRUE(blob_layout->MerkleTreeBlockCount().is_ok());
}

TEST(BlobLayoutTest, TotalBlockCountWithCompactFormatAndSharedBlockIsCorrect) {
  // The Merkle tree uses 2 blocks + 6016 bytes and the data uses 200 blocks + 10 bytes.
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize + 10;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto total_block_count = blob_layout->TotalBlockCount();
  ASSERT_TRUE(total_block_count.is_ok());
  // 200 data blocks + 2 Merkle blocks + 1 shared block.
  EXPECT_EQ(total_block_count.value(), 203u);
}

TEST(BlobLayoutTest, TotalBlockCountWithCompactFormatAndNonSharedBlockIsCorrect) {
  // The Merkle tree uses 2 blocks + 6016 bytes and the data uses 200 blocks.
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto total_block_count = blob_layout->TotalBlockCount();
  ASSERT_TRUE(total_block_count.is_ok());
  // 200 data blocks + 3 Merkle blocks.
  EXPECT_EQ(total_block_count.value(), 203u);
}

TEST(BlobLayoutTest, TotalBlockCountWithCompactFormatRequiringTooManyBlocksIsError) {
  // The Merkle tree and data both require less than 2^32 blocks but their sum is larger.
  ByteCountType file_size = (1ul << 39) * kBlockSize;
  ByteCountType data_size = (1ul << 31) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto total_block_count = blob_layout->TotalBlockCount();
  EXPECT_TRUE(total_block_count.is_error());
  // Verify that neither individually caused the error.
  EXPECT_TRUE(blob_layout->DataBlockCount().is_ok());
  EXPECT_TRUE(blob_layout->MerkleTreeBlockCount().is_ok());
}

TEST(BlobLayoutTest, HasMerkleTreeAndDataSharedBlockWithPaddedFormatReturnsFalse) {
  ByteCountType file_size = 4ul * kBlockSize;
  ByteCountType data_size = 4ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_and_data_share_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(merkle_tree_and_data_share_block.is_ok());
  EXPECT_EQ(merkle_tree_and_data_share_block.value(), false);
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndBlockAlignedDataReturnsFalse) {
  ByteCountType file_size = 4ul * kBlockSize;
  ByteCountType data_size = 3ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_and_data_share_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(merkle_tree_and_data_share_block.is_ok());
  EXPECT_FALSE(merkle_tree_and_data_share_block.value());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndBlockAlignedMerkleTreeReturnsFalse) {
  ByteCountType file_size = 256ul * kBlockSize;
  ByteCountType data_size = 10ul * kBlockSize + 1;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_and_data_share_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(merkle_tree_and_data_share_block.is_ok());
  EXPECT_FALSE(merkle_tree_and_data_share_block.value());
}

TEST(BlobLayoutTest, HasMerkleTreeAndDataSharedBlockWithCompactFormatAndNoMerkleTreeReturnsFalse) {
  ByteCountType file_size = kBlockSize;
  ByteCountType data_size = 10ul;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_and_data_share_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(merkle_tree_and_data_share_block.is_ok());
  EXPECT_FALSE(merkle_tree_and_data_share_block.value());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndMerkleTreeDoesNotFitInDataReturnsFalse) {
  ByteCountType file_size = 4ul * kBlockSize;
  ByteCountType data_size = 3ul * kBlockSize + (kBlockSize - 4 * kHashSize + 1);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_and_data_share_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(merkle_tree_and_data_share_block.is_ok());
  // The data takes 3 blocks + 8065 bytes and the Merkle tree takes 128 bytes.
  // 8065 + 128 = 8193 > 8192 So the Merkle tree and data will not share a block.
  EXPECT_FALSE(merkle_tree_and_data_share_block.value());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndMerkleTreeFitsInDataReturnsTrue) {
  ByteCountType file_size = 4ul * kBlockSize;
  ByteCountType data_size = 3ul * kBlockSize + (kBlockSize - 4 * kHashSize);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_and_data_share_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(merkle_tree_and_data_share_block.is_ok());
  // The data takes 3 blocks + 8064 bytes and the Merkle tree takes 128 bytes.
  // 8064 + 128 = 8192 So the Merkle tree and data will share a block.
  EXPECT_TRUE(merkle_tree_and_data_share_block.value());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndMerkleTreeRemainderFitsInDataReturnsTrue) {
  ByteCountType file_size = (256ul + 4) * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize + (kBlockSize - (6 * kHashSize) - 1);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_and_data_share_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(merkle_tree_and_data_share_block.is_ok());
  // The data takes 200 blocks + 7999 bytes and the Merkle tree takes 1 block + 192 bytes.
  // 799 + 192 = 8191 < 8192 So the Merkle tree and data will share a block.
  EXPECT_TRUE(merkle_tree_and_data_share_block.value());
}

TEST(BlobLayoutTest, FormatWithPaddedFormatIsCorrect) {
  ByteCountType file_size = 0;
  ByteCountType data_size = 0;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->Format(), BlobLayoutFormat::kPaddedMerkleTreeAtStart);
}

TEST(BlobLayoutTest, FormatWithCompactFormatIsCorrect) {
  ByteCountType file_size = 0;
  ByteCountType data_size = 0;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->Format(), BlobLayoutFormat::kCompactMerkleTreeAtEnd);
}

TEST(BlobLayoutTest, CreateFromInodeWithPaddedFormatAndUncompressedInodeIsCorrect) {
  // 21 blocks of data and 1 block for the Merkle tree.
  ByteCountType file_size = 20ul * kBlockSize + 50;
  BlockCountType block_count = 22;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 CreateInode(file_size, block_count), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), file_size);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), file_size);
  EXPECT_EQ(blob_layout->TotalBlockCount().value(), 22u);
}

TEST(BlobLayoutTest, CreateFromInodeWithPaddedFormatAndCompressedInodeIsCorrect) {
  // The Merkle tree takes 1 block leaving 19 for the compressed data.
  ByteCountType file_size = 26ul * kBlockSize + 50;
  BlockCountType block_count = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                  CreateCompressedInode(file_size, block_count), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), file_size);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), 19ul * kBlockSize);
  EXPECT_EQ(blob_layout->TotalBlockCount().value(), 20u);
}

TEST(BlobLayoutTest,
     CreateFromInodeWithPaddedFormatAndCompressedInodeAndTooLargeOfMerkleTreeIsError) {
  // The Merkle tree requires more than 2^32 blocks.
  ByteCountType file_size = (1ul << 50) * kBlockSize;
  BlockCountType block_count = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                  CreateCompressedInode(file_size, block_count), kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest,
     CreateFromInodeWithPaddedFormatAndCompressedInodeAndImpossibleBlockCountIsError) {
  // The Merkle tree requires more than 20 blocks leaving negative blocks for the compressed data.
  ByteCountType file_size = 256ul * 20 * kBlockSize;
  BlockCountType block_count = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                  CreateCompressedInode(file_size, block_count), kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest,
     CreateFromInodeWithCompactFormatAndUncompressedInodeAndNotSharedBlockIsCorrect) {
  ByteCountType file_size = 21ul * kBlockSize;
  BlockCountType block_count = 22;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 CreateInode(file_size, block_count), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), file_size);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), file_size);
  EXPECT_EQ(blob_layout->TotalBlockCount().value(), 22u);

  auto has_merkle_tree_and_data_shared_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(has_merkle_tree_and_data_shared_block.is_ok());
  EXPECT_FALSE(has_merkle_tree_and_data_shared_block.value());
}

TEST(BlobLayoutTest, CreateFromInodeWithCompactFormatAndUncompressedInodeAndSharedBlockIsCorrect) {
  ByteCountType file_size = 20ul * kBlockSize + 50;
  BlockCountType block_count = 21;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 CreateInode(file_size, block_count), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), file_size);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), file_size);
  EXPECT_EQ(blob_layout->TotalBlockCount().value(), 21u);

  auto has_merkle_tree_and_data_shared_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(has_merkle_tree_and_data_shared_block.is_ok());
  EXPECT_TRUE(has_merkle_tree_and_data_shared_block.value());
}

TEST(BlobLayoutTest, CreateFromInodeWithCompactFormatAndCompressedInodeAndSharedBlockIsCorrect) {
  ByteCountType file_size = 26ul * kBlockSize + 50;
  BlockCountType block_count = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                  CreateCompressedInode(file_size, block_count), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), file_size);
  // The file size rounded up is 27 blocks so the Merkle tree will contain 27 hashes.
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), (20ul * kBlockSize) - (27 * kHashSize));
  EXPECT_EQ(blob_layout->TotalBlockCount().value(), 20u);

  // CreateFromInode on a compressed Inode will always share a block unless the Merkle tree size is
  // a block multiple.
  auto has_merkle_tree_and_data_shared_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(has_merkle_tree_and_data_shared_block.is_ok());
  EXPECT_TRUE(has_merkle_tree_and_data_shared_block.value());
}

TEST(BlobLayoutTest, CreateFromInodeWithCompactFormatAndCompressedInodeAndNotSharedBlockIsCorrect) {
  // The Merkle tree takes exactly 1 block.
  ByteCountType file_size = 256ul * kBlockSize;
  BlockCountType block_count = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                  CreateCompressedInode(file_size, block_count), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), file_size);
  // The Merkle tree takes exactly 1 block leaving 19 for the data.
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), 19ul * kBlockSize);
  EXPECT_EQ(blob_layout->TotalBlockCount().value(), 20u);

  // Since the Merkle tree is a block multiple it couldn't have shared a block with the data.
  auto has_merkle_tree_and_data_shared_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(has_merkle_tree_and_data_shared_block.is_ok());
  EXPECT_FALSE(has_merkle_tree_and_data_shared_block.value());
}

TEST(BlobLayoutTest,
     CreateFromInodeWithCompactFormatAndCompressedInodeAndImpossibleBlockCountIsError) {
  // The Merkle tree requires more than 20 blocks leaving negative bytes for the compressed data.
  ByteCountType file_size = 256ul * 20 * kBlockSize;
  BlockCountType block_count = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                  CreateCompressedInode(file_size, block_count), kBlockSize);
  ASSERT_TRUE(blob_layout.is_error());
  EXPECT_EQ(blob_layout.error_value(), ZX_ERR_INVALID_ARGS);
}

}  // namespace
}  // namespace blobfs
