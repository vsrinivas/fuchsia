// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/minfs.h"

#include <lib/cksum.h>

#include <algorithm>
#include <utility>

#include <fvm/fvm-sparse.h>
#include <safemath/checked_math.h>

#include "fvm-host/format.h"
#include "src/storage/minfs/transaction_limits.h"

MinfsFormat::MinfsFormat(fbl::unique_fd fd, const char* type) : Format() {
  if (!strcmp(type, kDataTypeName)) {
    memcpy(type_, kDataType, sizeof(kDataType));
    flags_ |= fvm::kSparseFlagZxcrypt;

  } else if (!strcmp(type, kDataUnsafeTypeName)) {
    memcpy(type_, kDataType, sizeof(kDataType));

  } else if (!strcmp(type, kSystemTypeName)) {
    memcpy(type_, kSystemType, sizeof(kSystemType));

  } else if (!strcmp(type, kDefaultTypeName)) {
    memcpy(type_, kDefaultType, sizeof(kDefaultType));

  } else {
    fprintf(stderr, "Unrecognized type for minfs: %s\n", type);
    exit(-1);
  }

  struct stat s;

  if (fstat(fd.get(), &s) < 0) {
    fprintf(stderr, "error: minfs could not find end of file/device\n");
    exit(-1);
  } else if (s.st_size == 0) {
    fprintf(stderr, "minfs: failed to access block device\n");
    exit(-1);
  }

  off_t size = s.st_size / minfs::kMinfsBlockSize;

  if (minfs::Bcache::Create(std::move(fd), (uint32_t)size, &bc_) != ZX_OK) {
    fprintf(stderr, "error: cannot create block cache\n");
    exit(-1);
  }

  if (bc_->Readblk(0, &blk_) != ZX_OK) {
    fprintf(stderr, "minfs: could not read info block\n");
    exit(-1);
  }

  if (CheckSuperblock(&info_, bc_->Maxblk()) != ZX_OK) {
    fprintf(stderr, "Check info failed\n");
    exit(-1);
  }
}

zx_status_t MinfsFormat::MakeFvmReady(size_t slice_size, uint32_t vpart_index,
                                      FvmReservation* reserve) {
  memcpy(&fvm_blk_, &blk_, minfs::kMinfsBlockSize);
  fvm_info_.slice_size = static_cast<uint32_t>(slice_size);
  fvm_info_.flags |= minfs::kMinfsFlagFVM;

  if (fvm_info_.slice_size % minfs::kMinfsBlockSize) {
    fprintf(stderr, "minfs mkfs: Slice size not multiple of minfs block\n");
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t minimum_inodes = reserve->inodes().request.value_or(0);
  uint32_t ibm_blocks = fvm_info_.abm_block - fvm_info_.ibm_block;
  uint32_t ino_blocks = fvm_info_.integrity_start_block - fvm_info_.ino_block;

  if (minimum_inodes > fvm_info_.inode_count) {
    // If requested, reserve more inodes than originally allocated.
    ino_blocks = minfs::BlocksRequiredForInode(minimum_inodes);
    ibm_blocks = minfs::BlocksRequiredForBits(minimum_inodes);
  }

  uint32_t minimum_data_blocks = safemath::checked_cast<uint32_t>(
      fbl::round_up(reserve->data().request.value_or(0), minfs::kMinfsBlockSize) /
      minfs::kMinfsBlockSize);
  uint32_t abm_blocks = fvm_info_.ino_block - fvm_info_.abm_block;
  uint32_t dat_blocks = fvm_info_.block_count;

  if (minimum_data_blocks > fvm_info_.block_count) {
    // We are requested to reserve minimum_data_blocks, which is greater than data blocks
    // in fvm_info_. Ensure that we reserve sufficient space for allocation bitmap.
    abm_blocks = std::max(minfs::BlocksRequiredForBits(minimum_data_blocks), abm_blocks);
    dat_blocks = minimum_data_blocks;
  }

  uint32_t integrity_blocks = fvm_info_.dat_block - fvm_info_.integrity_start_block;

  fvm_info_.ibm_slices = safemath::checked_cast<uint32_t>(
      fvm::BlocksToSlices(fvm_info_.slice_size, minfs::kMinfsBlockSize, ibm_blocks));
  fvm_info_.abm_slices = safemath::checked_cast<uint32_t>(
      fvm::BlocksToSlices(fvm_info_.slice_size, minfs::kMinfsBlockSize, abm_blocks));
  fvm_info_.ino_slices = safemath::checked_cast<uint32_t>(
      fvm::BlocksToSlices(fvm_info_.slice_size, minfs::kMinfsBlockSize, ino_blocks));

  // TODO(planders): Weird things may happen if we grow the journal here while it contains valid
  //                 entries. Make sure to account for this case (or verify that the journal is
  //                 resolved prior to extension).
  minfs::TransactionLimits limits(fvm_info_);
  integrity_blocks = std::max(integrity_blocks, limits.GetRecommendedIntegrityBlocks());
  fvm_info_.integrity_slices = safemath::checked_cast<uint32_t>(
      fvm::BlocksToSlices(fvm_info_.slice_size, minfs::kMinfsBlockSize, integrity_blocks));
  fvm_info_.dat_slices = safemath::checked_cast<uint32_t>(
      fvm::BlocksToSlices(fvm_info_.slice_size, minfs::kMinfsBlockSize, dat_blocks));

  xprintf("Minfs: slice_size is %" PRIu64 ", block size is %zu\n", fvm_info_.slice_size,
          minfs::kMinfsBlockSize);
  xprintf("Minfs: ibm_blocks: %u, ibm_slices: %u\n", ibm_blocks, fvm_info_.ibm_slices);
  xprintf("Minfs: abm_blocks: %u, abm_slices: %u\n", abm_blocks, fvm_info_.abm_slices);
  xprintf("Minfs: ino_blocks: %u, ino_slices: %u\n", ino_blocks, fvm_info_.ino_slices);
  xprintf("Minfs: jnl_blocks: %u, jnl_slices: %u\n", integrity_blocks, fvm_info_.integrity_slices);
  xprintf("Minfs: dat_blocks: %u, dat_slices: %u\n", dat_blocks, fvm_info_.dat_slices);

  fvm_info_.inode_count = safemath::checked_cast<uint32_t>(
      fvm_info_.ino_slices * fvm_info_.slice_size / minfs::kMinfsInodeSize);
  fvm_info_.block_count = safemath::checked_cast<uint32_t>(
      fvm_info_.dat_slices * fvm_info_.slice_size / minfs::kMinfsBlockSize);

  fvm_info_.ibm_block = minfs::kFVMBlockInodeBmStart;
  fvm_info_.abm_block = minfs::kFVMBlockDataBmStart;
  fvm_info_.ino_block = minfs::kFVMBlockInodeStart;
  fvm_info_.integrity_start_block = minfs::kFvmSuperblockBackup;
  fvm_info_.dat_block = minfs::kFVMBlockDataStart;

  reserve->set_data_reserved(fvm_info_.dat_slices * fvm_info_.slice_size);
  reserve->set_inodes_reserved(fvm_info_.inode_count);
  reserve->set_total_bytes_reserved(CalculateVsliceCount(fvm_info_) * fvm_info_.slice_size);
  if (!reserve->Approved()) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  UpdateChecksum(&fvm_info_);

  zx_status_t status;
  // Check if bitmaps are the wrong size, slice extents run on too long, etc.
  if ((status = CheckSuperblock(&fvm_info_, bc_->Maxblk())) != ZX_OK) {
    fprintf(stderr, "Check info failed\n");
    return status;
  }

  fvm_ready_ = true;
  vpart_index_ = vpart_index;
  return ZX_OK;
}

zx_status_t MinfsFormat::GetVsliceRange(unsigned extent_index, vslice_info_t* vslice_info) const {
  CheckFvmReady();
  switch (extent_index) {
    case 0: {
      vslice_info->vslice_start = 0;
      vslice_info->slice_count = 1;
      vslice_info->block_offset = 0;
      vslice_info->block_count = 1;
      vslice_info->zero_fill = true;
      return ZX_OK;
    }
    case 1: {
      size_t blocks_per_slice = fvm_info_.slice_size / minfs::kMinfsBlockSize;
      uint32_t reserved_blocks = fvm_info_.ibm_slices * blocks_per_slice;
      vslice_info->vslice_start = minfs::kFVMBlockInodeBmStart;
      vslice_info->slice_count = fvm_info_.ibm_slices;
      vslice_info->block_offset = info_.ibm_block;

      // block_count is used to determine the extent_length, which tells the
      // paver, for the slices reserved how many blocks contain valid data.
      // This helps to keep sparse image small and helps paver to zero-out
      // block that are reserved but are not part of the sparse image file.
      vslice_info->block_count = std::min(info_.abm_block - info_.ibm_block, reserved_blocks);
      vslice_info->zero_fill = true;
      return ZX_OK;
    }
    case 2: {
      vslice_info->vslice_start = minfs::kFVMBlockDataBmStart;
      vslice_info->slice_count = fvm_info_.abm_slices;
      vslice_info->block_offset = info_.abm_block;
      vslice_info->block_count = info_.ino_block - info_.abm_block;
      vslice_info->zero_fill = true;
      return ZX_OK;
    }
    case 3: {
      vslice_info->vslice_start = minfs::kFVMBlockInodeStart;
      vslice_info->slice_count = fvm_info_.ino_slices;
      vslice_info->block_offset = info_.ino_block;
      vslice_info->block_count = info_.integrity_start_block - info_.ino_block;
      vslice_info->zero_fill = true;
      return ZX_OK;
    }
    case 4: {
      vslice_info->vslice_start = minfs::kFvmSuperblockBackup;
      vslice_info->slice_count = fvm_info_.integrity_slices;
      vslice_info->block_offset = info_.integrity_start_block;
      vslice_info->block_count = info_.dat_block - info_.integrity_start_block;
      vslice_info->zero_fill = false;
      return ZX_OK;
    }
    case 5: {
      vslice_info->vslice_start = minfs::kFVMBlockDataStart;
      vslice_info->slice_count = fvm_info_.dat_slices;
      vslice_info->block_offset = info_.dat_block;
      vslice_info->block_count = info_.block_count;
      vslice_info->zero_fill = false;
      return ZX_OK;
    }
  }

  return ZX_ERR_OUT_OF_RANGE;
}

zx_status_t MinfsFormat::GetSliceCount(uint32_t* slices_out) const {
  CheckFvmReady();
  *slices_out = safemath::checked_cast<uint32_t>(CalculateVsliceCount(fvm_info_));
  return ZX_OK;
}

zx_status_t MinfsFormat::FillBlock(size_t block_offset) {
  CheckFvmReady();
  if (block_offset == 0 || block_offset == info_.integrity_start_block) {
    // If this is the superblock or backup superblock location, write out the fvm info explicitly.
    memcpy(datablk, fvm_blk_, minfs::kMinfsBlockSize);
  } else if (bc_->Readblk(safemath::checked_cast<uint32_t>(block_offset), datablk) != ZX_OK) {
    fprintf(stderr, "minfs: could not read block\n");
    exit(-1);
  }
  return ZX_OK;
}

zx_status_t MinfsFormat::EmptyBlock() {
  CheckFvmReady();
  memset(datablk, 0, BlockSize());
  return ZX_OK;
}

void* MinfsFormat::Data() { return datablk; }

const char* MinfsFormat::Name() const { return kMinfsName; }

uint32_t MinfsFormat::BlockSize() const { return minfs::kMinfsBlockSize; }

uint32_t MinfsFormat::BlocksPerSlice() const {
  CheckFvmReady();
  return safemath::checked_cast<uint32_t>(fvm_info_.slice_size / BlockSize());
}
