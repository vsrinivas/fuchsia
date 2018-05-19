// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <bitmap/raw-bitmap.h>
#include <minfs/block-txn.h>

#include "allocator.h"

namespace minfs {
namespace {

// Returns the number of blocks necessary to store a pool containing
// |size| bits.
blk_t blocksForSize(size_t size) {
    return (static_cast<blk_t>(size) + kMinfsBlockBits - 1) / kMinfsBlockBits;
}

}  // namespace

zx_status_t Allocator::Initialize(const Bcache* bc, ReadTxn* txn, GrowHandler grow_cb,
                                  UsageHandler usage_cb, blk_t start_block,
                                  size_t pool_used, size_t pool_size) {
    grow_cb_ = fbl::move(grow_cb);
    usage_cb_ = fbl::move(usage_cb);
    start_block_ = start_block;
    pool_used_ = pool_used;

    blk_t pool_blocks = blocksForSize(pool_size);

    zx_status_t status;
    if ((status = map_.Reset(pool_blocks * kMinfsBlockBits)) != ZX_OK) {
        return status;
    }
    if ((status = map_.Shrink(pool_size)) != ZX_OK) {
        return status;
    }
#ifdef __Fuchsia__
    vmoid_t map_vmoid;
    if ((status = bc->AttachVmo(map_.StorageUnsafe()->GetVmo(), &map_vmoid)) != ZX_OK) {
        return status;
    }
    txn->Enqueue(map_vmoid, 0, start_block_, pool_blocks);
#else
    txn->Enqueue(map_.StorageUnsafe()->GetData(), 0, start_block_, pool_blocks);
#endif

    return ZX_OK;
}

zx_status_t Allocator::Allocate(WriteTxn* txn, size_t hint, size_t* out_index) {
    size_t bitoff_start;
    zx_status_t status;
    if ((status = map_.Find(false, hint, map_.size(), 1, &bitoff_start)) != ZX_OK) {
        if ((status = map_.Find(false, 0, hint, 1, &bitoff_start)) != ZX_OK) {
            size_t old_size = map_.size();
            if ((status = Extend(txn)) != ZX_OK) {
                return status;
            } else if ((status = map_.Find(false, old_size, map_.size(), 1,
                                           &bitoff_start)) != ZX_OK) {
                return status;
            }
        }
    }

    ZX_ASSERT(map_.Set(bitoff_start, bitoff_start + 1) == ZX_OK);
    Persist(txn, bitoff_start, 1);
    usage_cb_(txn, ++pool_used_);
    *out_index = bitoff_start;
    return ZX_OK;
}

void Allocator::Free(WriteTxn* txn, size_t index) {
    ZX_DEBUG_ASSERT(map_.Get(index, index + 1));
    map_.Clear(index, index + 1);
    Persist(txn, index, 1);
    usage_cb_(txn, --pool_used_);
}

zx_status_t Allocator::Extend(WriteTxn* txn) {
    TRACE_DURATION("minfs", "Minfs::Allocator::Extend");

    zx_status_t status;
    size_t pool_size;
    if ((status = grow_cb_(txn, &pool_size)) != ZX_OK) {
        fprintf(stderr, "minfs::Allocator::Extend failed to grow (on disk): %d\n", status);
        return status;
    }
    ZX_DEBUG_ASSERT(pool_size >= map_.size());

    // Update the block bitmap, write the new blocks back to disk
    // as "zero".
    size_t old_pool_size = map_.size();
    if ((status = map_.Grow(fbl::round_up(pool_size, kMinfsBlockBits))) != ZX_OK) {
        fprintf(stderr, "minfs::Allocator::Extend failed to Grow (in memory): %d\n", status);
        return ZX_ERR_NO_SPACE;
    }
    // Grow before shrinking to ensure the underlying storage is a multiple
    // of kMinfsBlockSize.
    map_.Shrink(pool_size);
    Persist(txn, old_pool_size, pool_size - old_pool_size);
    return ZX_OK;
}

void Allocator::Persist(WriteTxn* txn, size_t index, size_t count) {
    blk_t rel_block = static_cast<blk_t>(index) / kMinfsBlockBits;
    blk_t abs_block = start_block_ + rel_block;
    blk_t blk_count = blocksForSize(count);

#ifdef __Fuchsia__
    txn->Enqueue(map_.StorageUnsafe()->GetVmo(), rel_block, abs_block, blk_count);
#else
    txn->Enqueue(map_.StorageUnsafe()->GetData(), rel_block, abs_block, blk_count);
#endif
}

} // namespace minfs
