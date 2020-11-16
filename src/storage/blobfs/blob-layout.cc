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

constexpr char kPaddedMerkleTreeAtStartCommandLineArg[] = "padded";
constexpr char kCompactMerkleTreeAtEndCommandLineArg[] = "compact";

// Rounds up |byte_count| to the next multiple of |blobfs_block_size|.
ByteCountType RoundUpToBlockMultiple(ByteCountType byte_count, BlockSizeType blobfs_block_size) {
  return fbl::round_up(byte_count, blobfs_block_size);
}

// Returns the minimum number of blocks required to hold |byte_count| bytes.
BlockCountType BlocksRequiredForBytes(ByteCountType byte_count, BlockSizeType blobfs_block_size) {
  return RoundUpToBlockMultiple(byte_count, blobfs_block_size) / blobfs_block_size;
}

// Returns the maximum number of bytes that can fit in |block_count| blocks.
ByteCountType BytesThatFitInBlocks(BlockCountType block_count, BlockSizeType blobfs_block_size) {
  return ByteCountType{block_count} * blobfs_block_size;
}

// Returns the maximum number of bytes that can be represented by |BlockCountType| with a block size
// of |blobfs_block_size|.
ByteCountType MaxBytesThatCanFitInBlocks(BlockSizeType blobfs_block_size) {
  return BytesThatFitInBlocks(std::numeric_limits<BlockCountType>::max(), blobfs_block_size);
}

// Returns the maximum number of bytes that can be safely rounded up to the next block multiple of
// |blobfs_block_size|.
ByteCountType MaxBytesThatCanBeAligned(BlockSizeType blobfs_block_size) {
  return std::numeric_limits<ByteCountType>::max() - blobfs_block_size + 1;
}

class CompactMerkleTreeAtEndBlobLayout : public BlobLayout {
 public:
  CompactMerkleTreeAtEndBlobLayout(ByteCountType file_size, ByteCountType data_size,
                                   ByteCountType merkle_tree_size, BlockSizeType blobfs_block_size)
      : BlobLayout(file_size, data_size, merkle_tree_size, blobfs_block_size) {}

  BlockCountType DataBlockOffset() const override { return 0; }

  ByteCountType MerkleTreeOffset() const override {
    // The Merkle tree is aligned to end at the end of the blob.
    return TotalBlockCount() * blobfs_block_size() - MerkleTreeSize();
  }

  BlockCountType TotalBlockCount() const override {
    return BlocksRequiredForBytes(DataSizeUpperBound() + MerkleTreeSize(), blobfs_block_size());
  }

  bool HasMerkleTreeAndDataSharedBlock() const override {
    ByteCountType merkle_tree_block_remainder = MerkleTreeSize() % blobfs_block_size();
    ByteCountType data_block_remainder = DataSizeUpperBound() % blobfs_block_size();
    // If either the Merkle tree or data are a block multiple then they can't share a block.
    if (merkle_tree_block_remainder == 0 || data_block_remainder == 0) {
      return false;
    }
    return merkle_tree_block_remainder + data_block_remainder <= blobfs_block_size();
  }

  BlobLayoutFormat Format() const override { return BlobLayoutFormat::kCompactMerkleTreeAtEnd; }

  static zx::status<std::unique_ptr<CompactMerkleTreeAtEndBlobLayout>> CreateFromInode(
      const Inode& inode, BlockSizeType blobfs_block_size) {
    if (!inode.IsCompressed()) {
      // If the blob is not compressed then the size of the stored data is the file size.
      auto blob_layout = CreateFromSizes(inode.blob_size, inode.blob_size, blobfs_block_size);
      if (blob_layout.is_error()) {
        return blob_layout.take_error();
      }
      // For uncompressed blobs the inode's block count isn't needed to construct the BlobLayout but
      // we should make sure that it matches the calculated block count.
      if (blob_layout->TotalBlockCount() != inode.block_count) {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      return blob_layout;
    }
    // The exact compressed size of a blob isn't stored.  An upper bound can be determined from the
    // total size of the blob minus the Merkle tree size.  See fxbug.dev/44547.
    ByteCountType total_size = BytesThatFitInBlocks(inode.block_count, blobfs_block_size);
    ByteCountType merkle_tree_size = CalculateMerkleTreeSize(inode.blob_size);
    ByteCountType data_size;
    if (!safemath::CheckSub(total_size, merkle_tree_size).AssignIfValid(&data_size)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    if (!AreSizesValid(inode.blob_size, data_size, merkle_tree_size, blobfs_block_size)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    return zx::ok(std::make_unique<CompactMerkleTreeAtEndBlobLayout>(
        inode.blob_size, data_size, merkle_tree_size, blobfs_block_size));
  }

  static zx::status<std::unique_ptr<CompactMerkleTreeAtEndBlobLayout>> CreateFromSizes(
      ByteCountType file_size, ByteCountType data_size, BlockSizeType blobfs_block_size) {
    ByteCountType merkle_tree_size = CalculateMerkleTreeSize(file_size);
    if (!AreSizesValid(file_size, data_size, merkle_tree_size, blobfs_block_size)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    return zx::ok(std::make_unique<CompactMerkleTreeAtEndBlobLayout>(
        file_size, data_size, CalculateMerkleTreeSize(file_size), blobfs_block_size));
  }

 private:
  static ByteCountType CalculateMerkleTreeSize(ByteCountType file_size) {
    return digest::CalculateMerkleTreeSize(file_size, digest::kDefaultNodeSize,
                                           /*use_compact_format=*/true);
  }

  static bool AreSizesValid(ByteCountType file_size, ByteCountType data_size,
                            ByteCountType merkle_tree_size, BlockSizeType blobfs_block_size) {
    ByteCountType max_aligned_bytes = MaxBytesThatCanBeAligned(blobfs_block_size);
    ByteCountType max_block_bytes = MaxBytesThatCanFitInBlocks(blobfs_block_size);
    // Make sure that the file size can be rounded up to the next block multiple and the data and
    // Merkle tree can be represented by a number of blocks.  Requiring that the data and Merkle
    // tree can be represented by a number of blocks also ensure that they can be rounded up to the
    // next block multple.
    ZX_DEBUG_ASSERT(max_aligned_bytes > max_block_bytes);
    if ((file_size > max_aligned_bytes) || (data_size > max_block_bytes) ||
        (merkle_tree_size > max_block_bytes)) {
      return false;
    }
    uint64_t total_size;
    if (!safemath::CheckAdd(data_size, merkle_tree_size).AssignIfValid(&total_size)) {
      return false;
    }
    return total_size <= max_block_bytes;
  }
};

class PaddedMerkleTreeAtStartBlobLayout : public BlobLayout {
 public:
  PaddedMerkleTreeAtStartBlobLayout(ByteCountType file_size, ByteCountType data_size,
                                    ByteCountType merkle_tree_size, BlockSizeType blobfs_block_size)
      : BlobLayout(file_size, data_size, merkle_tree_size, blobfs_block_size) {}

  BlockCountType DataBlockOffset() const override {
    // The data starts at the beginning of the block following the Merkle tree.
    return MerkleTreeBlockCount();
  }

  ByteCountType MerkleTreeOffset() const override { return 0; }

  BlockCountType TotalBlockCount() const override {
    return DataBlockCount() + MerkleTreeBlockCount();
  }

  bool HasMerkleTreeAndDataSharedBlock() const override { return false; }

  BlobLayoutFormat Format() const override { return BlobLayoutFormat::kPaddedMerkleTreeAtStart; }

  static zx::status<std::unique_ptr<PaddedMerkleTreeAtStartBlobLayout>> CreateFromInode(
      const Inode& inode, BlockSizeType blobfs_block_size) {
    if (!inode.IsCompressed()) {
      // If the blob is not compressed then the size of the stored data is the file size.
      auto blob_layout = CreateFromSizes(inode.blob_size, inode.blob_size, blobfs_block_size);
      if (blob_layout.is_error()) {
        return blob_layout.take_error();
      }
      // For uncompressed blobs the inode's block count isn't needed to construct the BlobLayout but
      // we should make sure that it matches the calculated block count.
      if (blob_layout->TotalBlockCount() != inode.block_count) {
        return zx::error(ZX_ERR_INVALID_ARGS);
      }
      return blob_layout;
    }

    // The exact compressed size of a blob isn't stored.  An upper bound can be determined from the
    // total number of blocks minus the number of Merkle tree blocks.  See fxbug.dev/44547.
    ByteCountType merkle_tree_size = CalculateMerkleTreeSize(inode.blob_size);
    if (merkle_tree_size > MaxBytesThatCanFitInBlocks(blobfs_block_size)) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    BlockCountType merkle_tree_block_count =
        BlocksRequiredForBytes(merkle_tree_size, blobfs_block_size);
    BlockCountType data_block_count;
    if (!safemath::CheckSub(inode.block_count, merkle_tree_block_count)
             .AssignIfValid(&data_block_count)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    ByteCountType data_size = BytesThatFitInBlocks(data_block_count, blobfs_block_size);
    if (!AreSizesValid(inode.blob_size, data_size, merkle_tree_size, blobfs_block_size)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    return zx::ok(std::make_unique<PaddedMerkleTreeAtStartBlobLayout>(
        inode.blob_size, data_size, merkle_tree_size, blobfs_block_size));
  }

  static zx::status<std::unique_ptr<PaddedMerkleTreeAtStartBlobLayout>> CreateFromSizes(
      ByteCountType file_size, ByteCountType data_size, BlockSizeType blobfs_block_size) {
    ByteCountType merkle_tree_size = CalculateMerkleTreeSize(file_size);
    if (!AreSizesValid(file_size, data_size, merkle_tree_size, blobfs_block_size)) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    return zx::ok(std::make_unique<PaddedMerkleTreeAtStartBlobLayout>(
        file_size, data_size, merkle_tree_size, blobfs_block_size));
  }

 private:
  static ByteCountType CalculateMerkleTreeSize(ByteCountType file_size) {
    return digest::CalculateMerkleTreeSize(file_size, digest::kDefaultNodeSize,
                                           /*use_compact_format=*/false);
  }

  static bool AreSizesValid(ByteCountType file_size, ByteCountType data_size,
                            ByteCountType merkle_tree_size, BlockSizeType blobfs_block_size) {
    ByteCountType max_aligned_bytes = MaxBytesThatCanBeAligned(blobfs_block_size);
    ByteCountType max_block_bytes = MaxBytesThatCanFitInBlocks(blobfs_block_size);
    // Make sure that the file size can be rounded up to the next block multiple and the data and
    // Merkle tree can be represented by a number of blocks.  Requiring that the data and Merkle
    // tree can be represented by a number of blocks also ensure that they can be rounded up to the
    // next block multple.
    ZX_DEBUG_ASSERT(max_aligned_bytes > max_block_bytes);
    if ((file_size > max_aligned_bytes) || (data_size > max_block_bytes) ||
        (merkle_tree_size > max_block_bytes)) {
      return false;
    }
    return safemath::CheckAdd(BlocksRequiredForBytes(data_size, blobfs_block_size),
                              BlocksRequiredForBytes(merkle_tree_size, blobfs_block_size))
        .IsValid<BlockCountType>();
  }
};

}  // namespace

const char* BlobLayoutFormatToString(BlobLayoutFormat format) {
  switch (format) {
    case BlobLayoutFormat::kPaddedMerkleTreeAtStart:
      return "kPaddedMerkleTreeAtStart";
    case BlobLayoutFormat::kCompactMerkleTreeAtEnd:
      return "kCompactMerkleTreeAtEnd";
  }
}

const char* GetBlobLayoutFormatCommandLineArg(BlobLayoutFormat format) {
  switch (format) {
    case BlobLayoutFormat::kPaddedMerkleTreeAtStart:
      return kPaddedMerkleTreeAtStartCommandLineArg;
    case BlobLayoutFormat::kCompactMerkleTreeAtEnd:
      return kCompactMerkleTreeAtEndCommandLineArg;
  }
}

zx::status<BlobLayoutFormat> ParseBlobLayoutFormatCommandLineArg(const char* arg) {
  if (strcmp(kPaddedMerkleTreeAtStartCommandLineArg, arg) == 0) {
    return zx::ok(BlobLayoutFormat::kPaddedMerkleTreeAtStart);
  }
  if (strcmp(kCompactMerkleTreeAtEndCommandLineArg, arg) == 0) {
    return zx::ok(BlobLayoutFormat::kCompactMerkleTreeAtEnd);
  }
  return zx::error(ZX_ERR_INVALID_ARGS);
}

bool ShouldUseCompactMerkleTreeFormat(BlobLayoutFormat format) {
  switch (format) {
    case BlobLayoutFormat::kPaddedMerkleTreeAtStart:
      return false;
    case BlobLayoutFormat::kCompactMerkleTreeAtEnd:
      return true;
  }
}

BlobLayout::BlobLayout(ByteCountType file_size, ByteCountType data_size,
                       ByteCountType merkle_tree_size, BlockSizeType blobfs_block_size)
    : file_size_(file_size),
      data_size_(data_size),
      merkle_tree_size_(merkle_tree_size),
      blobfs_block_size_(blobfs_block_size) {}

BlobLayout::ByteCountType BlobLayout::FileSize() const { return file_size_; }

BlobLayout::ByteCountType BlobLayout::FileBlockAlignedSize() const {
  return RoundUpToBlockMultiple(file_size_, blobfs_block_size_);
}

BlobLayout::ByteCountType BlobLayout::DataSizeUpperBound() const { return data_size_; }

BlobLayout::ByteCountType BlobLayout::DataBlockAlignedSize() const {
  return RoundUpToBlockMultiple(data_size_, blobfs_block_size_);
}

BlobLayout::BlockCountType BlobLayout::DataBlockCount() const {
  return BlocksRequiredForBytes(data_size_, blobfs_block_size_);
}

BlobLayout::ByteCountType BlobLayout::MerkleTreeSize() const { return merkle_tree_size_; }

BlobLayout::ByteCountType BlobLayout::MerkleTreeBlockAlignedSize() const {
  return RoundUpToBlockMultiple(MerkleTreeSize(), blobfs_block_size_);
}

BlobLayout::BlockCountType BlobLayout::MerkleTreeBlockCount() const {
  return BlocksRequiredForBytes(MerkleTreeSize(), blobfs_block_size_);
}

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
