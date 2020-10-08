// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <limits>
#include <memory>

#include <blobfs/blob-layout.h>
#include <blobfs/format.h>
#include <digest/merkle-tree.h>
#include <digest/node-digest.h>
#include <fbl/algorithm.h>
#include <safemath/checked_math.h>

namespace blobfs {

namespace {

using ByteCountType = BlobLayout::ByteCountType;
using BlockCountType = BlobLayout::BlockCountType;
using BlockSizeType = BlobLayout::BlockSizeType;

// Rounds up |byte_count| to the next multiple of |blobfs_block_size|.
// Returns an error if the result doesn't fit in |ByteCountType|.
zx::status<ByteCountType> RoundUpToBlockMultiple(ByteCountType byte_count,
                                                 BlockSizeType blobfs_block_size) {
  ByteCountType max_byte_count = std::numeric_limits<ByteCountType>::max() - blobfs_block_size + 1;
  if (byte_count > max_byte_count) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  return zx::ok(fbl::round_up(byte_count, blobfs_block_size));
}

// Returns the minimum number of blocks required to hold |byte_count| bytes.
// Returns an error if the result doesn't fit in |BlockCountType|.
zx::status<BlockCountType> BlocksRequiredForBytes(ByteCountType byte_count,
                                                  BlockSizeType blobfs_block_size) {
  BlockCountType block_count;
  auto block_aligned_byte_count = RoundUpToBlockMultiple(byte_count, blobfs_block_size);
  if (block_aligned_byte_count.is_error()) {
    return block_aligned_byte_count.take_error();
  }
  if (!safemath::CheckDiv(block_aligned_byte_count.value(), blobfs_block_size)
           .AssignIfValid(&block_count)) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  return zx::ok(block_count);
}

// Returns the maxmimum number of bytes that can fit in |block_count| blocks.
ByteCountType BytesThatFitInBlocks(BlockCountType block_count, BlockSizeType blobfs_block_size) {
  return ByteCountType{block_count} * blobfs_block_size;
}

class CompactMerkleTreeAtEndBlobLayout : public BlobLayout {
 public:
  CompactMerkleTreeAtEndBlobLayout(ByteCountType file_size, ByteCountType data_size,
                                   ByteCountType merkle_tree_size, BlockSizeType blobfs_block_size)
      : BlobLayout(file_size, data_size, merkle_tree_size, blobfs_block_size) {}

  zx::status<BlockCountType> DataBlockOffset() const override { return zx::ok(0); }

  zx::status<BlockCountType> MerkleTreeBlockOffset() const override {
    // The Merkle tree is aligned to end at the end of the blob.  The Merkle tree's starting block
    // is the total number of blocks in the blob minus the number of blocks that the Merkle tree
    // spans.
    auto total_block_count = TotalBlockCount();
    if (total_block_count.is_error()) {
      return total_block_count.take_error();
    }
    auto merkle_tree_block_count = MerkleTreeBlockCount();
    if (merkle_tree_block_count.is_error()) {
      return merkle_tree_block_count.take_error();
    }
    BlockCountType merkle_tree_starting_block;
    if (!safemath::CheckSub(total_block_count.value(), merkle_tree_block_count.value())
             .AssignIfValid(&merkle_tree_starting_block)) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    return zx::ok(merkle_tree_starting_block);
  }

  ByteCountType MerkleTreeOffsetWithinBlockOffset() const override {
    // | block 6 | block 7 | block 8 | block 9 |
    //        |<-      Merkle tree size       >|
    // |<-  ->^ Merkle tree offset within block offset
    BlockSizeType blobfs_block_size = BlobfsBlockSize();
    ByteCountType merkle_tree_remainder = MerkleTreeSize() % blobfs_block_size;
    // If the Merkle tree size is a block multiple then it starts at the beginning of the block.
    if (merkle_tree_remainder == 0) {
      return 0;
    }
    return blobfs_block_size - merkle_tree_remainder;
  }

  zx::status<BlockCountType> TotalBlockCount() const override {
    ByteCountType total_size;
    if (!safemath::CheckAdd(DataSizeUpperBound(), MerkleTreeSize()).AssignIfValid(&total_size)) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    return BlocksRequiredForBytes(total_size, BlobfsBlockSize());
  }

  zx::status<bool> HasMerkleTreeAndDataSharedBlock() const override {
    // If the number of Merkle tree blocks + data blocks is greater than the total number of blocks
    // then 1 of the blocks must be shared.
    auto total_block_count = TotalBlockCount();
    if (total_block_count.is_error()) {
      return total_block_count.take_error();
    }
    auto data_block_count = DataBlockCount();
    if (data_block_count.is_error()) {
      return data_block_count.take_error();
    }
    auto merkle_tree_block_count = MerkleTreeBlockCount();
    if (merkle_tree_block_count.is_error()) {
      return merkle_tree_block_count.take_error();
    }
    BlockCountType block_sum;
    if (!safemath::CheckAdd(data_block_count.value(), merkle_tree_block_count.value())
             .AssignIfValid(&block_sum)) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    return zx::ok(block_sum > total_block_count.value());
  }

  BlobLayoutFormat Format() const override { return BlobLayoutFormat::kCompactMerkleTreeAtEnd; }

  static zx::status<std::unique_ptr<CompactMerkleTreeAtEndBlobLayout>> CreateFromInode(
      const Inode& inode, BlockSizeType blobfs_block_size) {
    if (!inode.IsCompressed()) {
      // If the blob is not compressed then the size of the stored data is the file size.
      return CreateFromSizes(inode.blob_size, inode.blob_size, blobfs_block_size);
    }
    // The exact compressed size of a blob isn't stored.  An upper bound can be determined from the
    // total size of the blob minus the Merkle tree size.  See fxbug.dev/44547.
    ByteCountType total_size = BytesThatFitInBlocks(inode.block_count, blobfs_block_size);
    ByteCountType merkle_tree_size = CalulateMerkleTreeSize(inode.blob_size);
    ByteCountType data_size;
    if (!safemath::CheckSub(total_size, merkle_tree_size).AssignIfValid(&data_size)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    return zx::ok(std::make_unique<CompactMerkleTreeAtEndBlobLayout>(
        inode.blob_size, data_size, merkle_tree_size, blobfs_block_size));
  }

  static zx::status<std::unique_ptr<CompactMerkleTreeAtEndBlobLayout>> CreateFromSizes(
      ByteCountType file_size, ByteCountType data_size, BlockSizeType blobfs_block_size) {
    return zx::ok(std::make_unique<CompactMerkleTreeAtEndBlobLayout>(
        file_size, data_size, CalulateMerkleTreeSize(file_size), blobfs_block_size));
  }

 private:
  static ByteCountType CalulateMerkleTreeSize(ByteCountType file_size) {
    // TODO(fxbug.dev/36663): Use the compact Merkle tree format.
    return digest::CalculateMerkleTreeSize(file_size, digest::kDefaultNodeSize);
  }
};

class PaddedMerkleTreeAtStartBlobLayout : public BlobLayout {
 public:
  PaddedMerkleTreeAtStartBlobLayout(ByteCountType file_size, ByteCountType data_size,
                                    ByteCountType merkle_tree_size, BlockSizeType blobfs_block_size)
      : BlobLayout(file_size, data_size, merkle_tree_size, blobfs_block_size) {}

  zx::status<BlockCountType> DataBlockOffset() const override {
    // The data starts at the beginning of the block following the Merkle tree.
    return MerkleTreeBlockCount();
  }

  zx::status<BlockCountType> MerkleTreeBlockOffset() const override { return zx::ok(0); }

  ByteCountType MerkleTreeOffsetWithinBlockOffset() const override { return 0; }

  zx::status<BlockCountType> TotalBlockCount() const override {
    // The total block count is the sum of the data blocks and the Merkle tree blocks.
    auto data_block_count = DataBlockCount();
    if (data_block_count.is_error()) {
      return data_block_count.take_error();
    }
    auto merkle_tree_block_count = MerkleTreeBlockCount();
    if (merkle_tree_block_count.is_error()) {
      return merkle_tree_block_count.take_error();
    }
    BlockCountType total_block_count;
    if (!safemath::CheckAdd(data_block_count.value(), merkle_tree_block_count.value())
             .AssignIfValid(&total_block_count)) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    return zx::ok(total_block_count);
  }

  zx::status<bool> HasMerkleTreeAndDataSharedBlock() const override { return zx::ok(false); }

  BlobLayoutFormat Format() const override { return BlobLayoutFormat::kPaddedMerkleTreeAtStart; }

  static zx::status<std::unique_ptr<PaddedMerkleTreeAtStartBlobLayout>> CreateFromInode(
      const Inode& inode, BlockSizeType blobfs_block_size) {
    if (!inode.IsCompressed()) {
      // If the blob is not compressed then the size of the stored data is the file size.
      return CreateFromSizes(inode.blob_size, inode.blob_size, blobfs_block_size);
    }

    // The exact compressed size of a blob isn't stored.  An upper bound can be determined from the
    // total number of blocks minus the number of Merkle tree blocks.  See fxbug.dev/44547.
    ByteCountType merkle_tree_size = CalculateMerkleTreeSize(inode.blob_size);
    auto merkle_tree_block_count = BlocksRequiredForBytes(merkle_tree_size, blobfs_block_size);
    if (merkle_tree_block_count.is_error()) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    BlockCountType data_block_count;
    if (!safemath::CheckSub(inode.block_count, merkle_tree_block_count.value())
             .AssignIfValid(&data_block_count)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    ByteCountType data_size = BytesThatFitInBlocks(data_block_count, blobfs_block_size);
    return zx::ok(std::make_unique<PaddedMerkleTreeAtStartBlobLayout>(
        inode.blob_size, data_size, merkle_tree_size, blobfs_block_size));
  }

  static zx::status<std::unique_ptr<PaddedMerkleTreeAtStartBlobLayout>> CreateFromSizes(
      ByteCountType file_size, ByteCountType data_size, BlockSizeType blobfs_block_size) {
    return zx::ok(std::make_unique<PaddedMerkleTreeAtStartBlobLayout>(
        file_size, data_size, CalculateMerkleTreeSize(file_size), blobfs_block_size));
  }

 private:
  static ByteCountType CalculateMerkleTreeSize(ByteCountType file_size) {
    return digest::CalculateMerkleTreeSize(file_size, digest::kDefaultNodeSize);
  }
};

}  // namespace

BlobLayout::BlobLayout(ByteCountType file_size, ByteCountType data_size,
                       ByteCountType merkle_tree_size, BlockSizeType blobfs_block_size)
    : file_size_(file_size),
      data_size_(data_size),
      merkle_tree_size_(merkle_tree_size),
      blobfs_block_size_(blobfs_block_size) {}

BlobLayout::ByteCountType BlobLayout::FileSize() const { return file_size_; }

zx::status<BlobLayout::ByteCountType> BlobLayout::FileBlockAlignedSize() const {
  return RoundUpToBlockMultiple(file_size_, blobfs_block_size_);
}

BlobLayout::ByteCountType BlobLayout::DataSizeUpperBound() const { return data_size_; }

zx::status<BlobLayout::ByteCountType> BlobLayout::DataBlockAlignedSize() const {
  return RoundUpToBlockMultiple(data_size_, blobfs_block_size_);
}

zx::status<BlobLayout::BlockCountType> BlobLayout::DataBlockCount() const {
  return BlocksRequiredForBytes(data_size_, blobfs_block_size_);
}

BlobLayout::ByteCountType BlobLayout::MerkleTreeSize() const { return merkle_tree_size_; }

zx::status<BlobLayout::ByteCountType> BlobLayout::MerkleTreeBlockAlignedSize() const {
  return RoundUpToBlockMultiple(MerkleTreeSize(), blobfs_block_size_);
}

zx::status<BlobLayout::BlockCountType> BlobLayout::MerkleTreeBlockCount() const {
  return BlocksRequiredForBytes(MerkleTreeSize(), blobfs_block_size_);
}

BlobLayout::BlockSizeType BlobLayout::BlobfsBlockSize() const { return blobfs_block_size_; }

zx::status<std::unique_ptr<BlobLayout>> BlobLayout::CreateFromInode(
    BlobLayoutFormat format, const Inode& inode, BlockSizeType blobfs_block_size) {
  switch (format) {
    case BlobLayoutFormat::kPaddedMerkleTreeAtStart: {
      auto layout = PaddedMerkleTreeAtStartBlobLayout::CreateFromInode(inode, blobfs_block_size);
      if (layout.is_error()) {
        return layout.take_error();
      }
      return layout.take_value();
    }
    case BlobLayoutFormat::kCompactMerkleTreeAtEnd: {
      auto layout = CompactMerkleTreeAtEndBlobLayout::CreateFromInode(inode, blobfs_block_size);
      if (layout.is_error()) {
        return layout.take_error();
      }
      return layout.take_value();
    }
  }
}

zx::status<std::unique_ptr<BlobLayout>> BlobLayout::CreateFromSizes(
    BlobLayoutFormat format, ByteCountType file_size, ByteCountType data_size,
    BlockSizeType blobfs_block_size) {
  switch (format) {
    case BlobLayoutFormat::kPaddedMerkleTreeAtStart: {
      auto layout = PaddedMerkleTreeAtStartBlobLayout::CreateFromSizes(file_size, data_size,
                                                                       blobfs_block_size);
      if (layout.is_error()) {
        return layout.take_error();
      }
      return layout.take_value();
    }
    case BlobLayoutFormat::kCompactMerkleTreeAtEnd: {
      auto layout = CompactMerkleTreeAtEndBlobLayout::CreateFromSizes(file_size, data_size,
                                                                      blobfs_block_size);
      if (layout.is_error()) {
        return layout.take_error();
      }
      return layout.take_value();
    }
  }
}

}  // namespace blobfs
