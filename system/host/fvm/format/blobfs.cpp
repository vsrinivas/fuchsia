// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include "fvm/format.h"

BlobfsFormat::BlobfsFormat(fbl::unique_fd fd, const char* type)
    : Format(), fd_(fbl::move(fd)) {
    if (!strcmp(type, kBlobTypeName)) {
        memcpy(type_, kBlobType, sizeof(kBlobType));
    } else if (!strcmp(type, kDefaultTypeName)) {
        memcpy(type_, kDefaultType, sizeof(kDefaultType));
    } else {
        fprintf(stderr, "Unrecognized type for blobfs: %s\n", type);
        exit(-1);
    }

    if (blobfs::readblk(fd_.get(), 0, reinterpret_cast<void*>(blk_)) < 0) {
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

zx_status_t BlobfsFormat::MakeFvmReady(size_t slice_size, uint32_t vpart_index) {
    memcpy(&fvm_blk_, &blk_, BlockSize());
    xprintf("fvm_info has data block count %" PRIu64 "\n", fvm_info_.data_block_count);
    fvm_info_.slice_size = slice_size;

    if (fvm_info_.slice_size % BlockSize()) {
        fprintf(stderr, "MakeFvmReady: Slice size not multiple of minfs block\n");
        return ZX_ERR_INVALID_ARGS;
    }

    fvm_info_.abm_slices = BlocksToSlices(BlockMapBlocks(info_));
    fvm_info_.ino_slices = BlocksToSlices(NodeMapBlocks(info_));
    fvm_info_.journal_slices = BlocksToSlices(JournalBlocks(info_));
    fvm_info_.dat_slices = BlocksToSlices(DataBlocks(info_));
    fvm_info_.vslice_count = 1 + fvm_info_.abm_slices + fvm_info_.ino_slices +
                             fvm_info_.dat_slices + fvm_info_.journal_slices;

    xprintf("Blobfs: slice_size is %" PRIu64 "\n", fvm_info_.slice_size);
    xprintf("Blobfs: abm_blocks: %" PRIu64 ", abm_slices: %u\n", BlockMapBlocks(info_),
            fvm_info_.abm_slices);
    xprintf("Blobfs: ino_blocks: %" PRIu64 ", ino_slices: %u\n", NodeMapBlocks(info_),
            fvm_info_.ino_slices);
    xprintf("Blobfs: jnl_blocks: %" PRIu64 ", journal_slices: %u\n", JournalBlocks(info_),
            fvm_info_.journal_slices);
    xprintf("Blobfs: dat_blocks: %" PRIu64 ", dat_slices: %u\n", DataBlocks(info_),
            fvm_info_.dat_slices);

    fvm_info_.inode_count = static_cast<uint32_t>(fvm_info_.ino_slices * fvm_info_.slice_size /
                                                  blobfs::kBlobfsInodeSize);
    fvm_info_.journal_block_count = SlicesToBlocks(fvm_info_.journal_slices);
    fvm_info_.data_block_count = SlicesToBlocks(fvm_info_.dat_slices);
    fvm_info_.flags |= blobfs::kBlobFlagFVM;

    zx_status_t status;
    if ((status = CheckSuperblock(&fvm_info_, blocks_)) != ZX_OK) {
        fprintf(stderr, "Check info failed\n");
        return status;
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
        vslice_info->block_count = 1;
        vslice_info->zero_fill = true;
        return ZX_OK;
    }
    case 1: {
        vslice_info->vslice_start = blobfs::kFVMBlockMapStart;
        vslice_info->slice_count = fvm_info_.abm_slices;
        vslice_info->block_offset = BlockMapStartBlock(info_);
        vslice_info->block_count = BlockMapBlocks(info_);
        vslice_info->zero_fill = true;
        return ZX_OK;
    }
    case 2: {
        vslice_info->vslice_start = blobfs::kFVMNodeMapStart;
        vslice_info->slice_count = fvm_info_.ino_slices;
        vslice_info->block_offset = NodeMapStartBlock(info_);
        vslice_info->block_count = NodeMapBlocks(info_);
        vslice_info->zero_fill = true;
        return ZX_OK;
    }
    case 3: {
        vslice_info->vslice_start = blobfs::kFVMJournalStart;
        vslice_info->slice_count = fvm_info_.journal_slices;
        vslice_info->block_offset = JournalStartBlock(info_);
        vslice_info->block_count = JournalBlocks(info_);
        vslice_info->zero_fill = false;
        return ZX_OK;
    }
    case 4: {
        vslice_info->vslice_start = blobfs::kFVMDataStart;
        vslice_info->slice_count = fvm_info_.dat_slices;
        vslice_info->block_offset = DataStartBlock(info_);
        vslice_info->block_count = DataBlocks(info_);
        vslice_info->zero_fill = false;
        return ZX_OK;
    }
    }

    return ZX_ERR_OUT_OF_RANGE;
}

zx_status_t BlobfsFormat::GetSliceCount(uint32_t* slices_out) const {
    CheckFvmReady();
    *slices_out = 1 + fvm_info_.abm_slices + fvm_info_.ino_slices + fvm_info_.journal_slices
                  + fvm_info_.dat_slices;
    return ZX_OK;
}

zx_status_t BlobfsFormat::FillBlock(size_t block_offset) {
    CheckFvmReady();
    // If we are reading the super block, make sure it is the fvm version and not the original
    if (block_offset == 0) {
        memcpy(datablk, fvm_blk_, BlockSize());
    } else if (blobfs::readblk(fd_.get(), block_offset, datablk) != ZX_OK) {
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

void* BlobfsFormat::Data() {
    return datablk;
}

void BlobfsFormat::Name(char* name) const {
    strcpy(name, kBlobfsName);
}

uint32_t BlobfsFormat::BlockSize() const {
    return blobfs::kBlobfsBlockSize;
}

uint32_t BlobfsFormat::BlocksPerSlice() const {
    CheckFvmReady();
    return fvm_info_.slice_size / BlockSize();
}

uint32_t BlobfsFormat::BlocksToSlices(uint32_t block_count) const {
    return fvm::BlocksToSlices(fvm_info_.slice_size, BlockSize(), block_count);
}

uint32_t BlobfsFormat::SlicesToBlocks(uint32_t slice_count) const {
    return fvm::SlicesToBlocks(fvm_info_.slice_size, BlockSize(), slice_count);
}
