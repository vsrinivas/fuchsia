// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <minfs/block-txn.h>

#include "allocator.h"
#include "storage.h"

namespace minfs {
namespace {

// Returns the number of blocks necessary to store a pool containing
// |size| bits.
blk_t BitmapBlocksForSize(size_t size) {
    return (static_cast<blk_t>(size) + kMinfsBlockBits - 1) / kMinfsBlockBits;
}

}  // namespace

uint32_t AllocatorStorage::PoolBlocks() const {
    return BitmapBlocksForSize(PoolTotal());
}

PersistentStorage::PersistentStorage(Bcache* bc, SuperblockManager* sb, size_t unit_size,
                                     GrowHandler grow_cb, AllocatorMetadata metadata) :
#ifdef __Fuchsia__
      bc_(bc), unit_size_(unit_size),
#endif
      sb_(sb),  grow_cb_(std::move(grow_cb)), metadata_(std::move(metadata)) {}

#ifdef __Fuchsia__
zx_status_t PersistentStorage::AttachVmo(const zx::vmo& vmo, fuchsia_hardware_block_VmoID* vmoid) {
    return bc_->AttachVmo(vmo, vmoid);
}
#endif

void PersistentStorage::Load(fs::ReadTxn* read_transaction, ReadData data) {
    read_transaction->Enqueue(data, 0, metadata_.MetadataStartBlock(), PoolBlocks());
}

zx_status_t PersistentStorage::Extend(WriteTxn* write_transaction, WriteData data,
                                      GrowMapCallback grow_map) {
#ifdef __Fuchsia__
    TRACE_DURATION("minfs", "Minfs::PersistentStorage::Extend");
    ZX_DEBUG_ASSERT(write_transaction != nullptr);
    if (!metadata_.UsingFvm()) {
        return ZX_ERR_NO_SPACE;
    }
    uint32_t data_slices_diff = 1;

    // Determine if we will have enough space in the bitmap slice
    // to grow |data_slices_diff| data slices.

    // How large is the bitmap right now?
    uint32_t bitmap_slices = metadata_.Fvm().MetadataSlices();
    uint32_t bitmap_blocks = metadata_.Fvm().UnitsPerSlices(bitmap_slices, kMinfsBlockSize);

    // How large does the bitmap need to be?
    uint32_t data_slices = metadata_.Fvm().DataSlices();
    uint32_t data_slices_new = data_slices + data_slices_diff;

    uint32_t pool_size = metadata_.Fvm().UnitsPerSlices(data_slices_new,
                                                        static_cast<uint32_t>(unit_size_));
    uint32_t bitmap_blocks_new = BitmapBlocksForSize(pool_size);

    if (bitmap_blocks_new > bitmap_blocks) {
        // TODO(smklein): Grow the bitmap another slice.
        // TODO(planders): Once we start growing the [block] bitmap,
        //                 we will need to start growing the journal as well.
        FS_TRACE_ERROR("Minfs allocator needs to increase bitmap size\n");
        return ZX_ERR_NO_SPACE;
    }

    // Make the request to the FVM.
    extend_request_t request;
    request.length = data_slices_diff;
    request.offset = metadata_.Fvm().BlocksToSlices(metadata_.DataStartBlock()) + data_slices;

    zx_status_t status;
    if ((status = bc_->FVMExtend(&request)) != ZX_OK) {
        FS_TRACE_ERROR("minfs::PersistentStorage::Extend failed to grow (on disk): %d\n", status);
        return status;
    }

    if (grow_cb_) {
        if ((status = grow_cb_(pool_size)) != ZX_OK) {
            FS_TRACE_ERROR("minfs::Allocator grow callback failure: %d\n", status);
            return status;
        }
    }

    // Extend the in memory representation of our allocation pool -- it grew!
    size_t old_pool_size;
    if ((status = grow_map(pool_size, &old_pool_size)) != ZX_OK) {
        return status;
    }

    metadata_.Fvm().SetDataSlices(data_slices_new);
    metadata_.SetPoolTotal(pool_size);
    sb_->Write(write_transaction);

    // Update the block bitmap.
    PersistRange(write_transaction, data, old_pool_size, pool_size - old_pool_size);
    return ZX_OK;
#else
    return ZX_ERR_NO_SPACE;
#endif
}

void PersistentStorage::PersistRange(WriteTxn* write_transaction, WriteData data, size_t index,
                                     size_t count) {
    ZX_DEBUG_ASSERT(write_transaction != nullptr);
    // Determine the blocks containing the first and last indices.
    blk_t first_rel_block = static_cast<blk_t>(index / kMinfsBlockBits);
    blk_t last_rel_block = static_cast<blk_t>((index + count - 1) / kMinfsBlockBits);

    // Calculate number of blocks based on the first and last blocks touched.
    blk_t block_count = last_rel_block - first_rel_block + 1;

    blk_t abs_block = metadata_.MetadataStartBlock() + first_rel_block;
    write_transaction->Enqueue(data, first_rel_block, abs_block, block_count);
}

void PersistentStorage::PersistAllocate(WriteTxn* write_transaction, size_t count) {
    ZX_DEBUG_ASSERT(write_transaction != nullptr);
    metadata_.PoolAllocate(static_cast<blk_t>(count));
    sb_->Write(write_transaction);
}

void PersistentStorage::PersistRelease(WriteTxn* write_transaction, size_t count) {
    ZX_DEBUG_ASSERT(write_transaction != nullptr);
    metadata_.PoolRelease(static_cast<blk_t>(count));
    sb_->Write(write_transaction);
}

} // namespace minfs
