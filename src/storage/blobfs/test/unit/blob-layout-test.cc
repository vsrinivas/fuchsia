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
  EXPECT_EQ(blob_layout->FileBlockAlignedSize(), 0ul);
}

TEST(BlobLayoutTest, FileBlockAlignedSizeWithAlignedFileSizeReturnsFileSize) {
  ByteCountType file_size = 10ul * kBlockSize;
  ByteCountType data_size = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileBlockAlignedSize(), file_size);
}

TEST(BlobLayoutTest, FileBlockAlignedSizeWithUnalignedFileSizeReturnsNextBlockMultiple) {
  ByteCountType file_size = 10ul * kBlockSize + 500;
  ByteCountType data_size = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileBlockAlignedSize(), 11ul * kBlockSize);
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
  EXPECT_EQ(blob_layout->DataBlockAlignedSize(), 0ul);
}

TEST(BlobLayoutTest, DataBlockAlignedSizeWithAlignedDataReturnsDataSizeUpperBound) {
  ByteCountType file_size = 8ul * kBlockSize + 30;
  ByteCountType data_size = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockAlignedSize(), data_size);
}

TEST(BlobLayoutTest, DataBlockAlignedSizeWithUnalignedDataReturnsNextBlockMultiple) {
  ByteCountType file_size = 8ul * kBlockSize + 30;
  ByteCountType data_size = 5ul * kBlockSize + 20;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockAlignedSize(), 6ul * kBlockSize);
}

TEST(BlobLayoutTest, DataBlockCountWithNoDataReturnsZero) {
  ByteCountType file_size = 0;
  ByteCountType data_size = 0;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockCount(), 0u);
}

TEST(BlobLayoutTest, DataBlockCountWithBlockAlignedDataIsCorrect) {
  ByteCountType file_size = 500ul * kBlockSize;
  ByteCountType data_size = 255ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockCount(), 255u);
}

TEST(BlobLayoutTest, DataBlockCountWithUnalignedDataIsCorrect) {
  ByteCountType file_size = 500ul * kBlockSize;
  ByteCountType data_size = 255ul * kBlockSize + 90;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockCount(), 256u);
}

TEST(BlobLayoutTest, DataBlockOffsetWithPaddedFormatAndNoMerkleTreeReturnsZero) {
  ByteCountType file_size = 100;
  ByteCountType data_size = 50;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockOffset(), 0u);
}

TEST(BlobLayoutTest, DataBlockOffsetWithPaddedFormatReturnsEndOfMerkleTree) {
  ByteCountType file_size = 600ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockOffset(), 4u);
}

TEST(BlobLayoutTest, DataBlockOffsetWithCompactFormatReturnsZero) {
  ByteCountType file_size = 100ul * kBlockSize;
  ByteCountType data_size = 100ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->DataBlockOffset(), 0u);
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
  EXPECT_EQ(blob_layout->MerkleTreeBlockAlignedSize(), 0ul);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithPaddedFormatAndAlignedMerkleTreeIsCorrect) {
  // In the padded format the Merkle tree is always a multiple of the node size so making the block
  // size the same as the node size will always produce a block aligned Merkle tree.
  ByteCountType file_size = 600ul * kNodeSize;
  ByteCountType data_size = 200ul * kNodeSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kNodeSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockAlignedSize(), 4ul * kNodeSize);
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
  EXPECT_EQ(blob_layout->MerkleTreeBlockAlignedSize(), 2ul * block_size);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithCompactFormatAndAlignedMerkleTreeIsCorrect) {
  ByteCountType file_size = 256ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockAlignedSize(), 1ul * kBlockSize);
}

TEST(BlobLayoutTest, MerkleTreeBlockAlignedSizeWithCompactFormatAndUnalignedMerkleTreeIsCorrect) {
  ByteCountType file_size = 600ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockAlignedSize(), 3ul * kBlockSize);
}

TEST(BlobLayoutTest, MerkleTreeBlockCountWithNoMerkleTreeReturnsZero) {
  ByteCountType file_size = kBlockSize;
  ByteCountType data_size = 300ul;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockCount(), 0u);
}

TEST(BlobLayoutTest, MerkleTreeBlockCountWithBlockAlignedMerkleTreeIsCorrect) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 255ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockCount(), 4u);
}

TEST(BlobLayoutTest, MerkleTreeBlockCountWithUnalignedMerkleTreeIsCorrect) {
  ByteCountType file_size = 600ul * kBlockSize;
  ByteCountType data_size = 255ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockCount(), 3u);
}

TEST(BlobLayoutTest, MerkleTreeBlockOffsetWithPaddedFormatReturnsZero) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockOffset(), 0ul);
}

TEST(BlobLayoutTest,
     MerkleTreeBlockOffsetWithCompactFormatAndNotSharingABlockReturnsDataBlockCount) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockOffset(), 200u);
}

TEST(BlobLayoutTest, MerkleTreeBlockOffsetWithCompactFormatAndSharingABlockIsCorrect) {
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize + 1;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->MerkleTreeBlockOffset(), 200u);
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
  EXPECT_EQ(blob_layout->TotalBlockCount(), 200u + 4u);
}

TEST(BlobLayoutTest, TotalBlockCountWithCompactFormatAndSharedBlockIsCorrect) {
  // The Merkle tree uses 2 blocks + 6016 bytes and the data uses 200 blocks + 10 bytes.
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize + 10;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // 200 data blocks + 2 Merkle blocks + 1 shared block.
  EXPECT_EQ(blob_layout->TotalBlockCount(), 203u);
}

TEST(BlobLayoutTest, TotalBlockCountWithCompactFormatAndNonSharedBlockIsCorrect) {
  // The Merkle tree uses 2 blocks + 6016 bytes and the data uses 200 blocks.
  ByteCountType file_size = 700ul * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // 200 data blocks + 3 Merkle blocks.
  EXPECT_EQ(blob_layout->TotalBlockCount(), 203u);
}

TEST(BlobLayoutTest, HasMerkleTreeAndDataSharedBlockWithPaddedFormatReturnsFalse) {
  ByteCountType file_size = 4ul * kBlockSize;
  ByteCountType data_size = 4ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->HasMerkleTreeAndDataSharedBlock(), false);
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndBlockAlignedDataReturnsFalse) {
  ByteCountType file_size = 4ul * kBlockSize;
  ByteCountType data_size = 3ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndBlockAlignedMerkleTreeReturnsFalse) {
  ByteCountType file_size = 256ul * kBlockSize;
  ByteCountType data_size = 10ul * kBlockSize + 1;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest, HasMerkleTreeAndDataSharedBlockWithCompactFormatAndNoMerkleTreeReturnsFalse) {
  ByteCountType file_size = kBlockSize;
  ByteCountType data_size = 10ul;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndMerkleTreeDoesNotFitInDataReturnsFalse) {
  ByteCountType file_size = 4ul * kBlockSize;
  ByteCountType data_size = 3ul * kBlockSize + (kBlockSize - 4 * kHashSize + 1);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // The data takes 3 blocks + 8065 bytes and the Merkle tree takes 128 bytes.
  // 8065 + 128 = 8193 > 8192 So the Merkle tree and data will not share a block.
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndMerkleTreeFitsInDataReturnsTrue) {
  ByteCountType file_size = 4ul * kBlockSize;
  ByteCountType data_size = 3ul * kBlockSize + (kBlockSize - 4 * kHashSize);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // The data takes 3 blocks + 8064 bytes and the Merkle tree takes 128 bytes.
  // 8064 + 128 = 8192 So the Merkle tree and data will share a block.
  EXPECT_TRUE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest,
     HasMerkleTreeAndDataSharedBlockWithCompactFormatAndMerkleTreeRemainderFitsInDataReturnsTrue) {
  ByteCountType file_size = (256ul + 4) * kBlockSize;
  ByteCountType data_size = 200ul * kBlockSize + (kBlockSize - (6 * kHashSize) - 1);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  // The data takes 200 blocks + 7999 bytes and the Merkle tree takes 1 block + 192 bytes.
  // 799 + 192 = 8191 < 8192 So the Merkle tree and data will share a block.
  EXPECT_TRUE(blob_layout->HasMerkleTreeAndDataSharedBlock());
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
  EXPECT_EQ(blob_layout->TotalBlockCount(), 22u);
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
  EXPECT_EQ(blob_layout->TotalBlockCount(), 20u);
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
     CreateFromInodeWithCompactFormatAndUncompressedInodeAndNotSharedBlockIsCorrect) {
  ByteCountType file_size = 21ul * kBlockSize;
  BlockCountType block_count = 22;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 CreateInode(file_size, block_count), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), file_size);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), file_size);
  EXPECT_EQ(blob_layout->TotalBlockCount(), 22u);
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest, CreateFromInodeWithCompactFormatAndUncompressedInodeAndSharedBlockIsCorrect) {
  ByteCountType file_size = 20ul * kBlockSize + 50;
  BlockCountType block_count = 21;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 CreateInode(file_size, block_count), kBlockSize);
  ASSERT_TRUE(blob_layout.is_ok());
  EXPECT_EQ(blob_layout->FileSize(), file_size);
  EXPECT_EQ(blob_layout->DataSizeUpperBound(), file_size);
  EXPECT_EQ(blob_layout->TotalBlockCount(), 21u);
  EXPECT_TRUE(blob_layout->HasMerkleTreeAndDataSharedBlock());
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
  EXPECT_EQ(blob_layout->TotalBlockCount(), 20u);

  // CreateFromInode on a compressed Inode will always share a block unless the Merkle tree size is
  // a block multiple.
  EXPECT_TRUE(blob_layout->HasMerkleTreeAndDataSharedBlock());
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
  EXPECT_EQ(blob_layout->TotalBlockCount(), 20u);

  // Since the Merkle tree is a block multiple it couldn't have shared a block with the data.
  EXPECT_FALSE(blob_layout->HasMerkleTreeAndDataSharedBlock());
}

TEST(BlobLayoutTest, CreateFromSizesWithPaddedFormatAndAndTooLargeOfFileSizeIsError) {
  ByteCountType file_size = std::numeric_limits<ByteCountType>::max();
  ByteCountType data_size = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithCompactFormatAndAndTooLargeOfFileSizeIsError) {
  ByteCountType file_size = std::numeric_limits<ByteCountType>::max();
  ByteCountType data_size = 5ul * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithPaddedFormatAndAndTooLargeOfDataSizeIsError) {
  ByteCountType file_size = (1ul << 35) * kBlockSize;
  ByteCountType data_size = (1ul << 34) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithCompactFormatAndAndTooLargeOfDataSizeIsError) {
  ByteCountType file_size = (1ul << 35) * kBlockSize;
  ByteCountType data_size = (1ul << 34) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithPaddedFormatAndAndTooLargeOfMerkleTreeSizeIsError) {
  ByteCountType file_size = ((1ul << 32) + 1) * kBlockSize * 256;
  ByteCountType data_size = (1ul << 30);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithCompactFormatAndTooLargeOfMerkleTreeIsError) {
  ByteCountType file_size = ((1ul << 32) + 1) * kBlockSize * 256;
  ByteCountType data_size = (1ul << 30);
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithPaddedFormatAndTooLargeOfTotalBlockCountIsError) {
  ByteCountType file_size = kBlockSize * 2;
  ByteCountType data_size = ((1ul << 32) - 1) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 file_size, data_size, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest, CreateFromSizesWithCompactFormatAndTooLargeOfTotalBlockCountIsError) {
  ByteCountType file_size = kBlockSize * 2;
  ByteCountType data_size = ((1ul << 32) - 1) * kBlockSize;
  auto blob_layout = BlobLayout::CreateFromSizes(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 file_size, data_size, kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest,
     CreateFromInodeWithPaddedFormatAndUncompressedAndBlockCountDoesNotMatchIsError) {
  ByteCountType file_size = 26ul * kBlockSize;
  BlockCountType block_count = 20;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 CreateInode(file_size, block_count), kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest,
     CreateFromInodeWithCompactFormatAndUncompressedAndBlockCountDoesNotMatchIsError) {
  ByteCountType file_size = 26ul * kBlockSize;
  BlockCountType block_count = 20;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 CreateInode(file_size, block_count), kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(
    BlobLayoutTest,
    CreateFromInodeWithPaddedFormatAndCompressedAndMerkleTreeBlockCountIsLargerThanBlockCountIsError) {
  ByteCountType file_size = 513ul * kBlockSize;
  BlockCountType block_count = 2;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                                                 CreateInode(file_size, block_count), kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

TEST(BlobLayoutTest,
     CreateFromInodeWithCompactFormatAndCompressedAndMerkleTreeSizeIsLargerThanStoredBytesIsError) {
  ByteCountType file_size = 513ul * kBlockSize;
  BlockCountType block_count = 2;
  auto blob_layout = BlobLayout::CreateFromInode(BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                                                 CreateInode(file_size, block_count), kBlockSize);
  EXPECT_TRUE(blob_layout.is_error());
}

}  // namespace
}  // namespace blobfs
