// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <limits>
#include <utility>

#include <safemath/checked_math.h>

#include "fvm-host/format.h"

namespace {

template <class T>
uint32_t ToU32(T in) {
  if (in > std::numeric_limits<uint32_t>::max()) {
    fprintf(stderr, "%s:%d out of range %" PRIuMAX "\n", __FILE__, __LINE__,
            safemath::checked_cast<uintmax_t>(in));
    exit(-1);
  }
  return safemath::checked_cast<uint32_t>(in);
}

}  // namespace

BlobfsFormat::BlobfsFormat(fbl::unique_fd fd, const char* type) : Format(), fd_(std::move(fd)) {
  if (!strcmp(type, kBlobTypeName)) {
    memcpy(type_, kBlobType, sizeof(kBlobType));
  } else if (!strcmp(type, kDefaultTypeName)) {
    memcpy(type_, kDefaultType, sizeof(kDefaultType));
  } else {
    fprintf(stderr, "Unrecognized type for blobfs: %s\n", type);
    exit(-1);
  }

  if (blobfs::ReadBlock(fd_.get(), 0, reinterpret_cast<void*>(blk_)) < 0) {
    fprintf(stderr, "blobfs: could not read info block\n");
    exit(-1);
  }

  if (blobfs::GetBlockCount(fd_.get(), &blocks_) != ZX_OK) {
    fprintf(stderr, "blobfs: cannot find end of underlying device\n");
    exit(-1);
  } else if (blobfs::CheckSuperblock(&info_, blocks_) != ZX_OK) {
    fprintf(stderr, "blobfs: Info check failed\n");
    exit(-1);
  }
}

BlobfsFormat::~BlobfsFormat() = default;

zx_status_t BlobfsFormat::ComputeSlices(uint64_t inode_count, uint64_t data_blocks,
                                        uint64_t journal_block_count) {
  auto abm_blocks = blobfs::BlocksRequiredForBits(data_blocks);
  auto ino_blocks = blobfs::BlocksRequiredForInode(inode_count);

  fvm_info_.abm_slices = BlocksToSlices(abm_blocks);
  fvm_info_.ino_slices = BlocksToSlices(ino_blocks);
  fvm_info_.journal_slices = BlocksToSlices(ToU32(journal_block_count));
  fvm_info_.dat_slices = BlocksToSlices(safemath::checked_cast<uint32_t>(data_blocks));

  fvm_info_.inode_count = safemath::checked_cast<uint32_t>(
      fvm_info_.ino_slices * fvm_info_.slice_size / blobfs::kBlobfsInodeSize);
  fvm_info_.journal_block_count = SlicesToBlocks(fvm_info_.journal_slices);
  fvm_info_.data_block_count = SlicesToBlocks(fvm_info_.dat_slices);
  fvm_info_.flags |= blobfs::kBlobFlagFVM;

  xprintf("Blobfs: slice_size is %" PRIu64 "\n", fvm_info_.slice_size);
  xprintf("Blobfs: abm_blocks: %" PRIu64 ", abm_slices: %u\n", BlockMapBlocks(fvm_info_),
          fvm_info_.abm_slices);
  xprintf("Blobfs: ino_blocks: %" PRIu64 ", ino_slices: %u\n", NodeMapBlocks(fvm_info_),
          fvm_info_.ino_slices);
  xprintf("Blobfs: jnl_blocks: %" PRIu64 ", jnl_slices: %u\n", JournalBlocks(fvm_info_),
          fvm_info_.journal_slices);
  xprintf("Blobfs: dat_blocks: %" PRIu64 ", dat_slices: %u\n", DataBlocks(fvm_info_),
          fvm_info_.dat_slices);

  zx_status_t status;
  if ((status = CheckSuperblock(&fvm_info_, blocks_)) != ZX_OK) {
    fprintf(stderr, "Check info failed\n");
    return status;
  }

  return ZX_OK;
}

zx_status_t BlobfsFormat::MakeFvmReady(size_t slice_size, uint32_t vpart_index,
                                       FvmReservation* reserve) {
  memcpy(&fvm_blk_, &blk_, BlockSize());
  xprintf("fvm_info has data block count %" PRIu64 "\n", fvm_info_.data_block_count);
  fvm_info_.slice_size = slice_size;

  if (fvm_info_.slice_size % BlockSize()) {
    fprintf(stderr, "MakeFvmReady: Slice size not multiple of minfs block\n");
    return ZX_ERR_INVALID_ARGS;
  }

  uint64_t minimum_data_blocks =
      fbl::round_up(reserve->data().request.value_or(0), BlockSize()) / BlockSize();
  if (minimum_data_blocks < fvm_info_.data_block_count) {
    minimum_data_blocks = fvm_info_.data_block_count;
  }

  uint64_t minimum_inode_count = reserve->inodes().request.value_or(0);
  if (minimum_inode_count < fvm_info_.inode_count) {
    minimum_inode_count = fvm_info_.inode_count;
  }

  zx_status_t status;
  if ((status = ComputeSlices(minimum_inode_count, minimum_data_blocks, JournalBlocks(info_))) !=
      ZX_OK) {
    return status;
  }

  // Lets see if we can increase journal size now
  uint64_t slice_limit = reserve->total_bytes().request.value_or(0) / slice_size;
  uint32_t vslice_count = blobfs::CalculateVsliceCount(fvm_info_);
  if (slice_limit > vslice_count) {
    // TODO(auradkar): This should use TransactionLimits
    uint64_t journal_block_count = blobfs::SuggestJournalBlocks(
        ToU32(JournalBlocks(fvm_info_)),
        ToU32((slice_limit - vslice_count) * slice_size / BlockSize()));
    // Above, we might have changed number of blocks allocated to the journal. This
    // might affect the number of allocated/reserved slices. Call ComputeSlices
    // again to adjust the count.
    if ((status = ComputeSlices(minimum_inode_count, minimum_data_blocks, journal_block_count)) !=
        ZX_OK) {
      return status;
    }
  }

  reserve->set_data_reserved(fvm_info_.data_block_count * BlockSize());
  reserve->set_inodes_reserved(fvm_info_.inode_count);
  reserve->set_total_bytes_reserved(SlicesToBlocks(vslice_count) * BlockSize());
  if (!reserve->Approved()) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  fvm_ready_ = true;
  vpart_index_ = vpart_index;
  return ZX_OK;
}

zx_status_t BlobfsFormat::GetVsliceRange(unsigned extent_index, vslice_info_t* vslice_info) const {
  CheckFvmReady();
  switch (extent_index) {
    case 0: {
      vslice_info->vslice_start = 0;
      vslice_info->slice_count = 1;
      vslice_info->block_offset = 0;
      vslice_info->block_count = ToU32(SuperblockBlocks(info_));
      vslice_info->zero_fill = true;
      return ZX_OK;
    }
    case 1: {
      vslice_info->vslice_start = blobfs::kFVMBlockMapStart;
      vslice_info->slice_count = fvm_info_.abm_slices;
      vslice_info->block_offset = ToU32(BlockMapStartBlock(info_));
      vslice_info->block_count = ToU32(BlockMapBlocks(info_));
      vslice_info->zero_fill = true;
      return ZX_OK;
    }
    case 2: {
      vslice_info->vslice_start = blobfs::kFVMNodeMapStart;
      vslice_info->slice_count = fvm_info_.ino_slices;
      vslice_info->block_offset = ToU32(NodeMapStartBlock(info_));
      vslice_info->block_count = ToU32(NodeMapBlocks(info_));
      vslice_info->zero_fill = true;
      return ZX_OK;
    }
    case 3: {
      vslice_info->vslice_start = blobfs::kFVMJournalStart;
      vslice_info->slice_count = fvm_info_.journal_slices;
      vslice_info->block_offset = ToU32(JournalStartBlock(info_));
      vslice_info->block_count = ToU32(JournalBlocks(info_));
      vslice_info->zero_fill = false;
      return ZX_OK;
    }
    case 4: {
      vslice_info->vslice_start = blobfs::kFVMDataStart;
      vslice_info->slice_count = fvm_info_.dat_slices;
      vslice_info->block_offset = ToU32(DataStartBlock(info_));
      vslice_info->block_count = ToU32(DataBlocks(info_));
      vslice_info->zero_fill = false;
      return ZX_OK;
    }
  }

  return ZX_ERR_OUT_OF_RANGE;
}

zx_status_t BlobfsFormat::GetSliceCount(uint32_t* slices_out) const {
  CheckFvmReady();
  *slices_out = 1 + fvm_info_.abm_slices + fvm_info_.ino_slices + fvm_info_.journal_slices +
                fvm_info_.dat_slices;
  return ZX_OK;
}

zx_status_t BlobfsFormat::FillBlock(size_t block_offset) {
  CheckFvmReady();
  // If we are reading the super block, make sure it is the fvm version and not the original
  if (block_offset == 0) {
    memcpy(datablk, fvm_blk_, BlockSize());
  } else if (blobfs::ReadBlock(fd_.get(), block_offset, datablk) != ZX_OK) {
    fprintf(stderr, "blobfs: could not read block\n");
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t BlobfsFormat::EmptyBlock() {
  CheckFvmReady();
  memset(datablk, 0, BlockSize());
  return ZX_OK;
}

void* BlobfsFormat::Data() { return datablk; }

const char* BlobfsFormat::Name() const { return kBlobfsName; }

uint32_t BlobfsFormat::BlockSize() const { return blobfs::kBlobfsBlockSize; }

uint32_t BlobfsFormat::BlocksPerSlice() const {
  CheckFvmReady();
  return ToU32(fvm_info_.slice_size / BlockSize());
}

uint32_t BlobfsFormat::BlocksToSlices(uint32_t block_count) const {
  return ToU32(fvm::BlocksToSlices(fvm_info_.slice_size, BlockSize(), block_count));
}

uint32_t BlobfsFormat::SlicesToBlocks(uint32_t slice_count) const {
  return ToU32(fvm::SlicesToBlocks(fvm_info_.slice_size, BlockSize(), slice_count));
}
