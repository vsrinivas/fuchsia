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

#include <safemath/checked_math.h>

#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/format.h"

#ifdef __Fuchsia__
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#endif

#include "src/storage/blobfs/common.h"

namespace blobfs {
namespace {

uint32_t GetBlobfsMajorVersionFromOptions(const FilesystemOptions& options) {
  if (options.blob_layout_format == BlobLayoutFormat::kCompactMerkleTreeAtEnd) {
    return kBlobfsCompactMerkleTreeVersion;
  }
  return 0x8;
}

bool CheckFilesystemAndDriverCompatibility(uint32_t major_version) {
  if (major_version == kBlobfsCurrentMajorVersion) {
    return true;
  }
  // Driver version 9 is compatible with filesystem version 8.
  if (major_version == 0x8 && kBlobfsCurrentMajorVersion == 0x9) {
    return true;
  }
  FX_LOGS(ERROR) << "Filesystem and Driver are incompatible. FS Version: " << std::setfill('0')
                 << std::setw(8) << std::hex << major_version
                 << ". Driver version: " << std::setw(8) << kBlobfsCurrentMajorVersion;
  return false;
}

}  // namespace

std::ostream& operator<<(std::ostream& stream, const Superblock& info) {
  return stream << "\ninfo.magic0: " << info.magic0 << "\ninfo.magic1: " << info.magic1
                << "\ninfo.major_version: " << info.major_version << "\ninfo.flags: " << info.flags
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
                << "\ninfo.oldest_minor_version: " << info.oldest_minor_version;
}

// Validate the metadata for the superblock, given a maximum number of available blocks.
zx_status_t CheckSuperblock(const Superblock* info, uint64_t max, bool quiet) {
  if ((info->magic0 != kBlobfsMagic0) || (info->magic1 != kBlobfsMagic1)) {
    if (!quiet)
      FX_LOGS(ERROR) << "bad magic";
    return ZX_ERR_INVALID_ARGS;
  }
  if (!CheckFilesystemAndDriverCompatibility(info->major_version)) {
    if (!quiet)
      FX_LOGS(ERROR) << *info;
    return ZX_ERR_INVALID_ARGS;
  }
  if (info->block_size != kBlobfsBlockSize) {
    if (!quiet)
      FX_LOGS(ERROR) << "block_size " << info->block_size << " unsupported" << *info;
    return ZX_ERR_INVALID_ARGS;
  }

  if (info->data_block_count < kMinimumDataBlocks) {
    if (!quiet)
      FX_LOGS(ERROR) << "Not enough space for minimum data partition";
    return ZX_ERR_NO_SPACE;
  }

  if (info->inode_count == 0) {
    if (!quiet)
      FX_LOGS(ERROR) << "Node table is zero-sized";
    return ZX_ERR_NO_SPACE;
  }

#ifdef __Fuchsia__
  if ((info->flags & kBlobFlagClean) == 0) {
    if (!quiet)
      FX_LOGS(WARNING) << "filesystem in dirty state. Was not unmounted cleanly.";
  } else {
    if (!quiet)
      FX_LOGS(INFO) << "filesystem in clean state.";
  }
#endif

  // Determine the number of blocks necessary for the block map and node map.
  uint64_t total_inode_size;
  if (mul_overflow(info->inode_count, sizeof(Inode), &total_inode_size)) {
    if (!quiet)
      FX_LOGS(ERROR) << "Multiplication overflow";
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint64_t node_map_size;
  if (mul_overflow(NodeMapBlocks(*info), kBlobfsBlockSize, &node_map_size)) {
    if (!quiet)
      FX_LOGS(ERROR) << "Multiplication overflow";
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (total_inode_size != node_map_size) {
    if (!quiet)
      FX_LOGS(ERROR) << "Inode table block must be entirely filled";
    return ZX_ERR_BAD_STATE;
  }

  if (info->journal_block_count < kMinimumJournalBlocks) {
    if (!quiet)
      FX_LOGS(ERROR) << "Not enough space for minimum journal partition";
    return ZX_ERR_NO_SPACE;
  }

  if (TotalBlocks(*info) > max) {
    if (!quiet)
      FX_LOGS(ERROR) << "Too large for device (" << max << " blocks): " << *info;
    return ZX_ERR_INVALID_ARGS;
  }
  if (info->flags & kBlobFlagFVM) {
    const size_t blocks_per_slice = info->slice_size / info->block_size;

    // Ensure that we have enough room in the first slice for the backup superblock, too. We could,
    // in theory, support a backup superblock which span past the first slice, but it would be a lot
    // of work given the tight coupling between FVM/blobfs, and the many places which assume that
    // the superblocks both fit within a slice.
    if (blobfs::kBlobfsBlockSize * 2 > info->slice_size) {
      if (!quiet)
        FX_LOGS(ERROR) << "Slice size doesn't fit backup superblock" << *info;
      return ZX_ERR_INVALID_ARGS;
    }

    size_t abm_blocks_needed = BlockMapBlocks(*info);
    size_t abm_blocks_allocated = info->abm_slices * blocks_per_slice;
    if (abm_blocks_needed > abm_blocks_allocated) {
      if (!quiet)
        FX_LOGS(ERROR) << "Not enough slices for block bitmap" << *info;
      return ZX_ERR_INVALID_ARGS;
    }
    if (abm_blocks_allocated + BlockMapStartBlock(*info) >= NodeMapStartBlock(*info)) {
      if (!quiet)
        FX_LOGS(ERROR) << "Block bitmap collides into node map" << *info;
      return ZX_ERR_INVALID_ARGS;
    }

    size_t ino_blocks_needed = NodeMapBlocks(*info);
    size_t ino_blocks_allocated = info->ino_slices * blocks_per_slice;
    if (ino_blocks_needed > ino_blocks_allocated) {
      if (!quiet)
        FX_LOGS(ERROR) << "Not enough slices for node map" << *info;
      return ZX_ERR_INVALID_ARGS;
    }
    if (ino_blocks_allocated + NodeMapStartBlock(*info) >= DataStartBlock(*info)) {
      if (!quiet)
        FX_LOGS(ERROR) << "Node bitmap collides into data blocks" << *info;
      return ZX_ERR_INVALID_ARGS;
    }

    size_t dat_blocks_needed = DataBlocks(*info);
    size_t dat_blocks_allocated = info->dat_slices * blocks_per_slice;
    if (dat_blocks_needed < kStartBlockMinimum) {
      if (!quiet)
        FX_LOGS(ERROR) << "Partition too small; no space left for data blocks" << *info;
      return ZX_ERR_INVALID_ARGS;
    }
    if (dat_blocks_needed > dat_blocks_allocated) {
      if (!quiet)
        FX_LOGS(ERROR) << "Not enough slices for data blocks" << *info;
      return ZX_ERR_INVALID_ARGS;
    }
    if (dat_blocks_allocated + DataStartBlock(*info) > std::numeric_limits<uint32_t>::max()) {
      if (!quiet)
        FX_LOGS(ERROR) << "Data blocks overflow uint32" << *info;
      return ZX_ERR_INVALID_ARGS;
    }
  }
  return ZX_OK;
}

uint64_t BlocksRequiredForInode(uint64_t inode_count) {
  return fbl::round_up(inode_count, kBlobfsInodesPerBlock) / kBlobfsInodesPerBlock;
}

uint64_t BlocksRequiredForBits(uint64_t bit_count) {
  return fbl::round_up(bit_count, kBlobfsBlockBits) / kBlobfsBlockBits;
}

void InitializeSuperblockOptions(const FilesystemOptions& options, Superblock* info) {
  memset(info, 0x00, sizeof(*info));
  info->magic0 = kBlobfsMagic0;
  info->magic1 = kBlobfsMagic1;
  info->major_version = GetBlobfsMajorVersionFromOptions(options);
  info->flags = kBlobFlagClean;
  info->block_size = kBlobfsBlockSize;
  info->alloc_block_count = kStartBlockMinimum;
  info->alloc_inode_count = 0;
  info->oldest_minor_version = options.oldest_minor_version;
}

zx_status_t InitializeSuperblock(uint64_t block_count, const FilesystemOptions& options,
                                 Superblock* info) {
  InitializeSuperblockOptions(options, info);

  // Round up |inode_count| to use a block-aligned amount.
  info->inode_count = BlocksRequiredForInode(options.num_inodes) * kBlobfsInodesPerBlock;

  // Temporarily set the data_block_count to the total block_count so we can estimate the number
  // of pre-data blocks.
  info->data_block_count = block_count;

  // The result of DataStartBlock(info) is based on the current value of info.data_block_count.
  // As a result, the block bitmap may have slightly more space allocated than is necessary.
  size_t usable_blocks =
      JournalStartBlock(*info) < block_count ? block_count - JournalStartBlock(*info) : 0;

  if (usable_blocks < kMinimumDataBlocks + kMinimumJournalBlocks) {
    info->journal_block_count = 0;
    info->data_block_count = 0;
    return ZX_ERR_NO_SPACE;
  }

  info->journal_block_count = kMinimumJournalBlocks;
  info->data_block_count = block_count - DataStartBlock(*info);
  return ZX_OK;
}

BlobLayoutFormat GetBlobLayoutFormat(const Superblock& info) {
  if (info.major_version >= 0x9) {
    return BlobLayoutFormat::kCompactMerkleTreeAtEnd;
  }
  return BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart;
}

namespace {

constexpr char kBlobVmoNamePrefix[] = "blob";
constexpr char kInactiveBlobVmoNamePrefix[] = "inactive-blob";
constexpr char kWritingBlobVmoNamePrefix[] = "writing-blob";

VmoNameBuffer FormatVmoName(const digest::Digest& digest, const char* prefix) {
  VmoNameBuffer buff = {};
  buff.AppendPrintf("%s-%.8s", prefix, digest.ToString().c_str());
  return buff;
}

}  // namespace

VmoNameBuffer FormatBlobDataVmoName(const digest::Digest& digest) {
  return FormatVmoName(digest, kBlobVmoNamePrefix);
}

VmoNameBuffer FormatInactiveBlobDataVmoName(const digest::Digest& digest) {
  return FormatVmoName(digest, kInactiveBlobVmoNamePrefix);
}

VmoNameBuffer FormatWritingBlobDataVmoName(const digest::Digest& digest) {
  return FormatVmoName(digest, kWritingBlobVmoNamePrefix);
}

}  // namespace blobfs
