// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>

#include <iomanip>
#include <limits>

#include <blobfs/blob-layout.h>
#include <blobfs/format.h>
#include <safemath/checked_math.h>

#ifdef __Fuchsia__
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>

#include <fvm/client.h>
#endif

#include <blobfs/common.h>

namespace blobfs {
namespace {

// Dumps the content of superblock.
std::ostream& operator<<(std::ostream& stream, const Superblock& info) {
  return stream << "\ninfo.magic0: " << info.magic0
      << "\ninfo.magic1: " << info.magic1
      << "\ninfo.format_version: " << info.format_version
      << "\ninfo.flags: " << info.flags
      << "\ninfo.block_size: " << info.block_size
      << "\ninfo.data_block_count: " << info.data_block_count
      << "\ninfo.journal_block_count: " << info.journal_block_count
      << "\ninfo.inode_count: " << info.inode_count
      << "\ninfo.alloc_block_count: " << info.alloc_block_count
      << "\ninfo.alloc_inode_count: " << info.alloc_inode_count
      << "\ninfo.slice_size: " << info.slice_size
      << "\ninfo.abm_slices: " << info.abm_slices
      << "\ninfo.ino_slices: " << info.ino_slices
      << "\ninfo.dat_slices: " << info.dat_slices
      << "\ninfo.journal_slices: " << info.journal_slices
      << "\ninfo.oldest_revision: " << info.oldest_revision;
}

uint32_t GetBlobfsFormatVersionFromOptions(const FilesystemOptions& options) {
  if (options.blob_layout_format == BlobLayoutFormat::kCompactMerkleTreeAtEnd) {
    return 0x9;
  }
  return 0x8;
}

bool CheckFilesystemAndDriverCompatibility(uint32_t format_version) {
  if (format_version == kBlobfsCurrentFormatVersion) {
    return true;
  }
  // Driver version 9 is compatible with filesystem version 8.
  if (format_version == 0x8 && kBlobfsCurrentFormatVersion == 0x9) {
    return true;
  }
  FX_LOGS(ERROR) << "Filesystem and Driver are incompatible. FS Version: " << std::setfill('0')
                 << std::setw(8) << std::hex << format_version
                 << ". Driver version: " << std::setw(8) << kBlobfsCurrentFormatVersion;
  return false;
}

}  // namespace

// Validate the metadata for the superblock, given a maximum number of
// available blocks.
zx_status_t CheckSuperblock(const Superblock* info, uint64_t max) {
  if ((info->magic0 != kBlobfsMagic0) || (info->magic1 != kBlobfsMagic1)) {
    FX_LOGS(ERROR) << "bad magic";
    return ZX_ERR_INVALID_ARGS;
  }
  if (!CheckFilesystemAndDriverCompatibility(info->format_version)) {
    FX_LOGS(ERROR) << *info;
    return ZX_ERR_INVALID_ARGS;
  }
  if (info->block_size != kBlobfsBlockSize) {
    FX_LOGS(ERROR) << "bsz " << info->block_size << " unsupported" << *info;
    return ZX_ERR_INVALID_ARGS;
  }

  if (info->data_block_count < kMinimumDataBlocks) {
    FX_LOGS(ERROR) << "Not enough space for minimum data partition";
    return ZX_ERR_NO_SPACE;
  }

#ifdef __Fuchsia__
  if ((info->flags & kBlobFlagClean) == 0) {
    FX_LOGS(WARNING) << "filesystem in dirty state. Was not unmounted cleanly.";
  } else {
    FX_LOGS(INFO) << "filesystem in clean state.";
  }
#endif

  // Determine the number of blocks necessary for the block map and node map.
  uint64_t total_inode_size;
  if (mul_overflow(info->inode_count, sizeof(Inode), &total_inode_size)) {
    FX_LOGS(ERROR) << "Multiplication overflow";
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t node_map_size;
  if (mul_overflow(NodeMapBlocks(*info), kBlobfsBlockSize, &node_map_size)) {
    FX_LOGS(ERROR) << "Multiplication overflow";
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (total_inode_size != node_map_size) {
    FX_LOGS(ERROR) << "Inode table block must be entirely filled";
    return ZX_ERR_BAD_STATE;
  }

  if (info->journal_block_count < kMinimumJournalBlocks) {
    FX_LOGS(ERROR) << "Not enough space for minimum journal partition";
    return ZX_ERR_NO_SPACE;
  }

  if ((info->flags & kBlobFlagFVM) == 0) {
    if (TotalBlocks(*info) > max) {
      FX_LOGS(ERROR) << "too large for device" << *info;
      return ZX_ERR_INVALID_ARGS;
    }
  } else {
    const size_t blocks_per_slice = info->slice_size / info->block_size;

    size_t abm_blocks_needed = BlockMapBlocks(*info);
    size_t abm_blocks_allocated = info->abm_slices * blocks_per_slice;
    if (abm_blocks_needed > abm_blocks_allocated) {
      FX_LOGS(ERROR) << "Not enough slices for block bitmap" << *info;
      return ZX_ERR_INVALID_ARGS;
    } else if (abm_blocks_allocated + BlockMapStartBlock(*info) >= NodeMapStartBlock(*info)) {
      FX_LOGS(ERROR) << "Block bitmap collides into node map" << *info;
      return ZX_ERR_INVALID_ARGS;
    }

    size_t ino_blocks_needed = NodeMapBlocks(*info);
    size_t ino_blocks_allocated = info->ino_slices * blocks_per_slice;
    if (ino_blocks_needed > ino_blocks_allocated) {
      FX_LOGS(ERROR) << "Not enough slices for node map" << *info;
      return ZX_ERR_INVALID_ARGS;
    } else if (ino_blocks_allocated + NodeMapStartBlock(*info) >= DataStartBlock(*info)) {
      FX_LOGS(ERROR) << "Node bitmap collides into data blocks" << *info;
      return ZX_ERR_INVALID_ARGS;
    }

    size_t dat_blocks_needed = DataBlocks(*info);
    size_t dat_blocks_allocated = info->dat_slices * blocks_per_slice;
    if (dat_blocks_needed < kStartBlockMinimum) {
      FX_LOGS(ERROR) << "Partition too small; no space left for data blocks" << *info;
      return ZX_ERR_INVALID_ARGS;
    } else if (dat_blocks_needed > dat_blocks_allocated) {
      FX_LOGS(ERROR) << "Not enough slices for data blocks" << *info;
      return ZX_ERR_INVALID_ARGS;
    } else if (dat_blocks_allocated + DataStartBlock(*info) >
               std::numeric_limits<uint32_t>::max()) {
      FX_LOGS(ERROR) << "Data blocks overflow uint32" << *info;
      return ZX_ERR_INVALID_ARGS;
    }
  }
  return ZX_OK;
}

uint32_t CalculateVsliceCount(const Superblock& superblock) {
  // Account for an additional slice for the superblock itself.
  return safemath::checked_cast<uint32_t>(1 + static_cast<uint64_t>(superblock.abm_slices) +
                                          static_cast<uint64_t>(superblock.ino_slices) +
                                          static_cast<uint64_t>(superblock.dat_slices) +
                                          static_cast<uint64_t>(superblock.journal_slices));
}

uint32_t BlocksRequiredForInode(uint64_t inode_count) {
  return safemath::checked_cast<uint32_t>(fbl::round_up(inode_count, kBlobfsInodesPerBlock) /
                                          kBlobfsInodesPerBlock);
}

uint32_t BlocksRequiredForBits(uint64_t bit_count) {
  return safemath::checked_cast<uint32_t>(fbl::round_up(bit_count, kBlobfsBlockBits) /
                                          kBlobfsBlockBits);
}

uint32_t SuggestJournalBlocks(uint32_t current, uint32_t available) { return current + available; }

void InitializeSuperblock(uint64_t block_count, const FilesystemOptions& options,
                          Superblock* info) {
  uint64_t inodes = kBlobfsDefaultInodeCount;
  memset(info, 0x00, sizeof(*info));
  info->magic0 = kBlobfsMagic0;
  info->magic1 = kBlobfsMagic1;
  info->format_version = GetBlobfsFormatVersionFromOptions(options);
  info->flags = kBlobFlagClean;
  info->block_size = kBlobfsBlockSize;
  // TODO(planders): Consider modifying the inode count if we are low on space.
  //                 It doesn't make sense to have fewer data blocks than inodes.
  info->inode_count = inodes;
  info->alloc_block_count = kStartBlockMinimum;
  info->alloc_inode_count = 0;
  info->oldest_revision = options.oldest_revision;

  // Temporarily set the data_block_count to the total block_count so we can estimate the number
  // of pre-data blocks.
  info->data_block_count = block_count;

  // The result of DataStartBlock(info) is based on the current value of info.data_block_count.
  // As a result, the block bitmap may have slightly more space allocated than is necessary.
  size_t usable_blocks =
      JournalStartBlock(*info) < block_count ? block_count - JournalStartBlock(*info) : 0;

  // Determine allocation for the journal vs. data blocks based on the number of blocks remaining.
  if (usable_blocks >= kDefaultJournalBlocks * 2) {
    // Regular-sized partition, capable of fitting a data region
    // at least as large as the journal. Give all excess blocks
    // to the data region.
    info->journal_block_count = kDefaultJournalBlocks;
    info->data_block_count = usable_blocks - kDefaultJournalBlocks;
  } else if (usable_blocks >= kMinimumDataBlocks + kMinimumJournalBlocks) {
    // On smaller partitions, give both regions the minimum amount of space,
    // and split the remainder. The choice of where to allocate the "remainder"
    // is arbitrary.
    const size_t remainder_blocks = usable_blocks - (kMinimumDataBlocks + kMinimumJournalBlocks);
    const size_t remainder_for_journal = remainder_blocks / 2;
    const size_t remainder_for_data = remainder_blocks - remainder_for_journal;
    info->journal_block_count = kMinimumJournalBlocks + remainder_for_journal;
    info->data_block_count = kMinimumDataBlocks + remainder_for_data;
  } else {
    // Error, partition too small.
    info->journal_block_count = 0;
    info->data_block_count = 0;
  }
}

BlobLayoutFormat GetBlobLayoutFormat(const Superblock& info) {
  if (info.format_version >= 0x9) {
    return BlobLayoutFormat::kCompactMerkleTreeAtEnd;
  }
  return BlobLayoutFormat::kPaddedMerkleTreeAtStart;
}

constexpr char kBlobVmoNamePrefix[] = "blob";
constexpr char kBlobCompressedVmoNamePrefix[] = "blobCompressed";
constexpr char kBlobMerkleVmoNamePrefix[] = "blob-merkle";

void FormatBlobDataVmoName(uint32_t node_index, fbl::StringBuffer<ZX_MAX_NAME_LEN>* out) {
  out->Clear();
  out->AppendPrintf("%s-%x", kBlobVmoNamePrefix, node_index);
}

void FormatBlobCompressedVmoName(uint32_t node_index, fbl::StringBuffer<ZX_MAX_NAME_LEN>* out) {
  out->Clear();
  out->AppendPrintf("%s-%x", kBlobCompressedVmoNamePrefix, node_index);
}

void FormatBlobMerkleVmoName(uint32_t node_index, fbl::StringBuffer<ZX_MAX_NAME_LEN>* out) {
  out->Clear();
  out->AppendPrintf("%s-%x", kBlobMerkleVmoNamePrefix, node_index);
}

}  // namespace blobfs
