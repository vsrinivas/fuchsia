// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fvm/format.h"

MinfsFormat::MinfsFormat(fbl::unique_fd fd, const char* type)
    : Format() {
    if (!strcmp(type, kDataTypeName)) {
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

    if (minfs::Bcache::Create(&bc_, fbl::move(fd), (uint32_t)size) < 0) {
        fprintf(stderr, "error: cannot create block cache\n");
        exit(-1);
    }

    if (bc_->Readblk(0, &blk_) != ZX_OK) {
        fprintf(stderr, "minfs: could not read info block\n");
        exit(-1);
    }

    if (minfs_check_info(&info_, bc_.get()) != ZX_OK) {
        fprintf(stderr, "Check info failed\n");
        exit(-1);
    }
}

zx_status_t MinfsFormat::MakeFvmReady(size_t slice_size, uint32_t vpart_index) {
    memcpy(&fvm_blk_, &blk_, minfs::kMinfsBlockSize);
    fvm_info_.slice_size = slice_size;

    if (fvm_info_.slice_size % minfs::kMinfsBlockSize) {
        fprintf(stderr, "minfs mkfs: Slice size not multiple of minfs block\n");
        return ZX_ERR_INVALID_ARGS;
    }

    size_t kBlocksPerSlice = fvm_info_.slice_size / minfs::kMinfsBlockSize;
    uint32_t ibm_blocks = info_.abm_block - info_.ibm_block;
    uint32_t abm_blocks = info_.ino_block - info_.abm_block;
    uint32_t ino_blocks = info_.dat_block - info_.ino_block;
    uint32_t dat_blocks = info_.block_count;

    fvm_info_.ibm_slices = (ibm_blocks + kBlocksPerSlice - 1) / kBlocksPerSlice;
    fvm_info_.abm_slices = (abm_blocks + kBlocksPerSlice - 1) / kBlocksPerSlice;
    fvm_info_.ino_slices = (ino_blocks + kBlocksPerSlice - 1) / kBlocksPerSlice;
    fvm_info_.dat_slices = (dat_blocks + kBlocksPerSlice - 1) / kBlocksPerSlice;
    fvm_info_.vslice_count = 1 + fvm_info_.ibm_slices + fvm_info_.abm_slices +
                             fvm_info_.ino_slices + fvm_info_.dat_slices;

    xprintf("Minfs: slice_size is %" PRIu64 "u, kBlocksPerSlice is %zu\n", fvm_info_.slice_size,
            kBlocksPerSlice);
    xprintf("Minfs: ibm_blocks: %u, ibm_slices: %u\n", ibm_blocks, fvm_info_.ibm_slices);
    xprintf("Minfs: abm_blocks: %u, abm_slices: %u\n", abm_blocks, fvm_info_.abm_slices);
    xprintf("Minfs: ino_blocks: %u, ino_slices: %u\n", ino_blocks, fvm_info_.ino_slices);
    xprintf("Minfs: dat_blocks: %u, dat_slices: %u\n", dat_blocks, fvm_info_.dat_slices);

    fvm_info_.inode_count = static_cast<uint32_t>(fvm_info_.ino_slices * fvm_info_.slice_size /
                                                  minfs::kMinfsInodeSize);
    fvm_info_.block_count = static_cast<uint32_t>(fvm_info_.dat_slices * fvm_info_.slice_size /
                                                  minfs::kMinfsBlockSize);

    fvm_info_.ibm_block = minfs::kFVMBlockInodeBmStart;
    fvm_info_.abm_block = minfs::kFVMBlockDataBmStart;
    fvm_info_.ino_block = minfs::kFVMBlockInodeStart;
    fvm_info_.dat_block = minfs::kFVMBlockDataStart;
    fvm_info_.flags |= minfs::kMinfsFlagFVM;

    zx_status_t status;
    // Check if bitmaps are the wrong size, slice extents run on too long, etc.
    if ((status = minfs_check_info(&fvm_info_, bc_.get())) != ZX_OK) {
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
        return ZX_OK;
    }
    case 1: {
        vslice_info->vslice_start = minfs::kFVMBlockInodeBmStart;
        vslice_info->slice_count = fvm_info_.ibm_slices;
        vslice_info->block_offset = info_.ibm_block;
        vslice_info->block_count = info_.abm_block - info_.ibm_block;
        return ZX_OK;
    }
    case 2: {
        vslice_info->vslice_start = minfs::kFVMBlockDataBmStart;
        vslice_info->slice_count = fvm_info_.abm_slices;
        vslice_info->block_offset = info_.abm_block;
        vslice_info->block_count = info_.ino_block - info_.abm_block;
        return ZX_OK;
    }
    case 3: {
        vslice_info->vslice_start = minfs::kFVMBlockInodeStart;
        vslice_info->slice_count = fvm_info_.ino_slices;
        vslice_info->block_offset = info_.ino_block;
        vslice_info->block_count = info_.dat_block - info_.ino_block;
        return ZX_OK;
    }
    case 4: {
        vslice_info->vslice_start = minfs::kFVMBlockDataStart;
        vslice_info->slice_count = fvm_info_.dat_slices;
        vslice_info->block_offset = info_.dat_block;
        vslice_info->block_count = info_.block_count;
        return ZX_OK;
    }
    }

    return ZX_ERR_OUT_OF_RANGE;
}

zx_status_t MinfsFormat::GetSliceCount(uint32_t* slices_out) const {
    CheckFvmReady();
    *slices_out = 1 + fvm_info_.ibm_slices + fvm_info_.abm_slices + fvm_info_.ino_slices
                  + fvm_info_.dat_slices;
    return ZX_OK;
}

zx_status_t MinfsFormat::FillBlock(size_t block_offset) {
    CheckFvmReady();
    if (block_offset == 0) {
        memcpy(datablk, fvm_blk_, minfs::kMinfsBlockSize);
    } else if (bc_->Readblk(block_offset, datablk) != ZX_OK) {
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

void* MinfsFormat::Data() {
    return datablk;
}

void MinfsFormat::Name(char* name) const {
    strcpy(name, kMinfsName);
}

uint32_t MinfsFormat::BlockSize() const {
    return minfs::kMinfsBlockSize;
}

uint32_t MinfsFormat::BlocksPerSlice() const {
    CheckFvmReady();
    return fvm_info_.slice_size / BlockSize();
}
