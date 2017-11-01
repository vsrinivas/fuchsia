// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "format.h"

static constexpr char kBlobstoreName[] = "blobstore";
static constexpr uint8_t kBlobstoreType[] = GUID_BLOBFS_VALUE;

BlobfsFormat::BlobfsFormat(int fd, const char* type)
    : Format(), fd_(fd) {
    if (!strcmp(type, kBlobstoreName)) {
        memcpy(type_, kBlobstoreType, sizeof(kBlobstoreType));
    } else {
        fprintf(stderr, "Unrecognized type for blobstore: %s\n", type);
        exit(-1);
    }

    if (blobstore::readblk(fd_, 0, reinterpret_cast<void*>(blk_)) < 0) {
        fprintf(stderr, "blobstore: could not read info block\n");
        exit(-1);
    }

    if (blobstore::blobstore_get_blockcount(fd_, &blocks_) != ZX_OK) {
        fprintf(stderr, "blobstore: cannot find end of underlying device\n");
        exit(-1);
    } else if (blobstore::blobstore_check_info(&info_, blocks_) != ZX_OK) {
        fprintf(stderr, "blobstore: Info check failed\n");
        exit(-1);
    }
}

BlobfsFormat::~BlobfsFormat() {
    close(fd_);
}

zx_status_t BlobfsFormat::MakeFvmReady(size_t slice_size, uint32_t vpart_index) {
    memcpy(&fvm_blk_, &blk_, BlockSize());
    printf("fvm_info has block count %" PRIu64 "\n", fvm_info_.block_count);
    fvm_info_.slice_size = slice_size;

    if (fvm_info_.slice_size % BlockSize()) {
        fprintf(stderr, "MakeFvmReady: Slice size not multiple of minfs block\n");
        return ZX_ERR_INVALID_ARGS;
    }

    size_t kBlocksPerSlice = fvm_info_.slice_size / BlockSize();

    fvm_info_.abm_slices = (BlockMapBlocks(info_) + kBlocksPerSlice - 1) / kBlocksPerSlice;
    fvm_info_.ino_slices = (NodeMapBlocks(info_) + kBlocksPerSlice - 1) / kBlocksPerSlice;
    fvm_info_.dat_slices = (DataBlocks(info_) + kBlocksPerSlice - 1) / kBlocksPerSlice;
    fvm_info_.vslice_count = 1 + fvm_info_.abm_slices + fvm_info_.ino_slices +
                             fvm_info_.dat_slices;

    printf("Blobfs: slice_size is %" PRIu64 ", kBlocksPerSlice is %" PRIu64 "\n", fvm_info_.slice_size,
           kBlocksPerSlice);
    printf("Blobfs: abm_blocks: %" PRIu64 ", abm_slices: %u\n", BlockMapBlocks(info_),
           fvm_info_.abm_slices);
    printf("Blobfs: ino_blocks: %" PRIu64 ", ino_slices: %u\n", NodeMapBlocks(info_),
           fvm_info_.ino_slices);
    printf("Blobfs: dat_blocks: %" PRIu64 ", dat_slices: %u\n", DataBlocks(info_),
           fvm_info_.dat_slices);

    fvm_info_.inode_count = static_cast<uint32_t>(fvm_info_.ino_slices * fvm_info_.slice_size /
                                                  blobstore::kBlobstoreInodeSize);
    fvm_info_.block_count = static_cast<uint32_t>(fvm_info_.dat_slices * fvm_info_.slice_size /
                                                  blobstore::kBlobstoreBlockSize);

    fvm_info_.flags |= blobstore::kBlobstoreFlagFVM;

    zx_status_t status;
    if ((status = blobstore_check_info(&fvm_info_, blocks_)) != ZX_OK) {
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
        return ZX_OK;
    }
    case 1: {
        vslice_info->vslice_start = blobstore::kFVMBlockMapStart;
        vslice_info->slice_count = fvm_info_.abm_slices;
        vslice_info->block_offset = BlockMapStartBlock(info_);
        vslice_info->block_count = BlockMapBlocks(info_);
        return ZX_OK;
    }
    case 2: {
        vslice_info->vslice_start = blobstore::kFVMNodeMapStart;
        vslice_info->slice_count = fvm_info_.ino_slices;
        vslice_info->block_offset = NodeMapStartBlock(info_);
        vslice_info->block_count = NodeMapBlocks(info_);
        return ZX_OK;
    }
    case 3: {
        vslice_info->vslice_start = blobstore::kFVMDataStart;
        vslice_info->slice_count = fvm_info_.dat_slices;
        vslice_info->block_offset = DataStartBlock(info_);
        vslice_info->block_count = DataBlocks(info_);
        return ZX_OK;
    }
    }

    return ZX_ERR_OUT_OF_RANGE;
}

zx_status_t BlobfsFormat::FillBlock(size_t block_offset) {
    CheckFvmReady();
    // If we are reading the super block, make sure it is the fvm version and not the original
    if (block_offset == 0) {
        memcpy(datablk, fvm_blk_, BlockSize());
    } else if (blobstore::readblk(fd_, block_offset, datablk) != ZX_OK) {
        fprintf(stderr, "blobstore: could not read block\n");
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
    strcpy(name, kBlobstoreName);
}

uint32_t BlobfsFormat::BlockSize() const {
    return blobstore::kBlobstoreBlockSize;
}

uint32_t BlobfsFormat::BlocksPerSlice() const {
    CheckFvmReady();
    return fvm_info_.slice_size / BlockSize();
}
