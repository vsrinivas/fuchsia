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
  EXPECT_EQ(blob_layout->MerkleTreeSize(), 4ul * kBlockSize);
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

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithAlignedMerkleTreeIsCorrect) {
  ByteCountType file_size = 600ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_block_aligned_size = blob_layout->MerkleTreeBlockAlignedSize();
  ASSERT_TRUE(merkle_tree_block_aligned_size.is_ok());
  EXPECT_EQ(merkle_tree_block_aligned_size.value(), 4ul * kBlockSize);
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

TEST(BlobLayoutTest, MerkleTreeBlockOffsetWithCompactFormatReturnsDataBlockCount) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
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

// TODO(fxbug.dev/36663): The offset won't always be zero with the compact Merkle trees.
TEST(BlobLayoutTest, MerkleTreeOffsetWithinBlockOffsetWithCompactFormatReturnsZero) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
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

// TODO(fxbug.dev/36663): There are more complicated cases with compact Merkle trees.
TEST(BlobLayoutTest, TotalBlockCountWithCompactFormatIsCorrect) {
  // The Merkle tree uses 4 blocks and the data uses 201 blocks.
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize + 10;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto total_block_count = blob_layout->TotalBlockCount();
  ASSERT_TRUE(total_block_count.is_ok());
  EXPECT_EQ(total_block_count.value(), 201u + 4u);
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

// TODO(fxbug.dev/36663): The block may be shared with compact Merkle trees.
TEST(BlobLayoutTest, HasMerkleTreeAndDataSharedBlockWithCompactFormatReturnsFalse) {
  ByteCountType file_size = 4ul * kBlockSize;
  ByteCountType data_size = 3ul * kBlockSize + 10;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  auto merkle_tree_and_data_share_block = blob_layout->HasMerkleTreeAndDataSharedBlock();
  ASSERT_TRUE(merkle_tree_and_data_share_block.is_ok());
  EXPECT_EQ(merkle_tree_and_data_share_block.value(), false);
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

TEST(BlobLayoutTest, CreateFromInodeWithCompactFormatAndUncompressedInodeIsCorrect) {
  ByteCountType file_size = 20ul * kBlockSize + 50;
  BlockCountType block_count = 22;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 CreateInode(file_size, block_count), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), file_size);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), file_size);
  EXPECT_EQ(blob_layout->TotalBlockCount().value(), 22u);
}

TEST(BlobLayoutTest, CreateFromInodeWithCompactFormatAndCompressedInodeIsCorrect) {
  // The Merkle tree takes 1 block leaving 19 for the compressed data.
  ByteCountType file_size = 26ul * kBlockSize + 50;
  BlockCountType block_count = 20;
  auto blob_layout =
      BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                  CreateCompressedInode(file_size, block_count), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), file_size);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), 19ul * kBlockSize);
  EXPECT_EQ(blob_layout->TotalBlockCount().value(), 20u);
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

// TODO(fxbug.dev/36663): Add a test with an unaligned Merkle tree.

}  // namespace
}  // namespace blobfs
