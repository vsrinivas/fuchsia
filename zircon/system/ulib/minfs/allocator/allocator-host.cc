// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <mutex>
#include <utility>

#include <stdlib.h>
#include <string.h>

#include <bitmap/raw-bitmap.h>
#include <minfs/block-txn.h>

#include "allocator.h"

namespace minfs {

Allocator::~Allocator() {
#ifdef __Fuchsia__
    AutoLock lock(&lock_);
    ZX_DEBUG_ASSERT(swap_in_.num_bits() == 0);
    ZX_DEBUG_ASSERT(swap_out_.num_bits() == 0);
#endif
}

zx_status_t Allocator::Create(fs::ReadTxn* txn, fbl::unique_ptr<AllocatorStorage> storage,
                              fbl::unique_ptr<Allocator>* out) FS_TA_NO_THREAD_SAFETY_ANALYSIS {
    // Ignore thread-safety analysis on the |allocator| object; no one has an
    // external reference to it yet.
    zx_status_t status;
    fbl::unique_ptr<Allocator> allocator(new Allocator(std::move(storage)));

    blk_t total_blocks = allocator->storage_->PoolTotal();
    blk_t pool_blocks = allocator->storage_->PoolBlocks();
    if ((status = allocator->map_.Reset(pool_blocks * kMinfsBlockBits)) != ZX_OK) {
        return status;
    }
    if ((status = allocator->map_.Shrink(total_blocks)) != ZX_OK) {
        return status;
    }

#ifdef __Fuchsia__
    fuchsia_hardware_block_VmoID map_vmoid;
    if ((status = allocator->storage_->AttachVmo(allocator->map_.StorageUnsafe()->GetVmo(),
                                                 &map_vmoid)) != ZX_OK) {
        return status;
    }
    allocator->storage_->Load(txn, map_vmoid.id);
#else
    allocator->storage_->Load(txn, allocator->GetMapDataLocked());
#endif
    *out = std::move(allocator);
    return ZX_OK;
}

size_t Allocator::GetAvailable() const {
    AutoLock lock(&lock_);
    return GetAvailableLocked();
}

size_t Allocator::GetAvailableLocked() const {
    size_t total_reserved = reserved_;
#ifdef __Fuchsia__
    total_reserved += swap_in_.num_bits();
#endif
    ZX_DEBUG_ASSERT(storage_->PoolAvailable() >= total_reserved);
    return storage_->PoolAvailable() - total_reserved;
}

void Allocator::Free(WriteTxn* txn, size_t index) {
    AutoLock lock(&lock_);
#ifdef __Fuchsia__
    ZX_DEBUG_ASSERT(!swap_out_.GetOne(index));
#endif
    ZX_DEBUG_ASSERT(map_.GetOne(index));

    map_.ClearOne(index);
    storage_->PersistRange(txn, GetMapDataLocked(), index, 1);
    storage_->PersistRelease(txn, 1);

    if (index < first_free_) {
        first_free_ = index;
    }
}

zx_status_t Allocator::GrowMapLocked(size_t new_size, size_t* old_size) {
    ZX_DEBUG_ASSERT(new_size >= map_.size());
    *old_size = map_.size();
    // Grow before shrinking to ensure the underlying storage is a multiple
    // of kMinfsBlockSize.
    zx_status_t status;
    if ((status = map_.Grow(fbl::round_up(new_size, kMinfsBlockBits))) != ZX_OK) {
        fprintf(stderr, "minfs::Allocator failed to Grow (in memory): %d\n", status);
        return ZX_ERR_NO_SPACE;
    }

    map_.Shrink(new_size);
    return ZX_OK;
}

WriteData Allocator::GetMapDataLocked() const {
#ifdef __Fuchsia__
    return map_.StorageUnsafe()->GetVmo().get();
#else
    return map_.StorageUnsafe()->GetData();
#endif
}

zx_status_t Allocator::Reserve(AllocatorPromiseKey, WriteTxn* txn, size_t count,
                               AllocatorPromise* promise) {
    AutoLock lock(&lock_);
    if (GetAvailableLocked() < count) {
        // If we do not have enough free elements, attempt to extend the partition.
        auto grow_map = ([this](size_t pool_size, size_t* old_pool_size)
                FS_TA_NO_THREAD_SAFETY_ANALYSIS {
            return this->GrowMapLocked(pool_size, old_pool_size);
        });

        zx_status_t status;
        // TODO(planders): Allow Extend to take in count.
        if ((status = storage_->Extend(txn, GetMapDataLocked(), grow_map)) != ZX_OK) {
            return status;
        }

        ZX_DEBUG_ASSERT(GetAvailableLocked() >= count);
    }

    reserved_ += count;
    return ZX_OK;
}

size_t Allocator::FindLocked() const {
    ZX_DEBUG_ASSERT(reserved_ > 0);
    size_t start = first_free_;

    while (true) {
        // Search for first free element in the map.
        size_t index;
        ZX_ASSERT(map_.Find(false, start, map_.size(), 1, &index) == ZX_OK);

#ifdef __Fuchsia__
        // Although this element is free in |map_|, it may be used by another in-flight transaction
        // in |swap_in_|. Ensure it does not collide before returning it.

        // Check the next |kBits| elements in the map. This number is somewhat arbitrary, but it
        // will prevent us from scanning the entire map if all following elements are unset.
        size_t upper_limit = fbl::min(index + bitmap::kBits, map_.size());
        map_.Scan(index, upper_limit, false, &upper_limit);
        ZX_DEBUG_ASSERT(upper_limit <= map_.size());

        // Check the reserved map to see if there are any free blocks from |index| to
        // |index + max_len|.
        size_t out;
        zx_status_t status = swap_in_.Find(false, index, upper_limit, 1, &out);

        // If we found a valid range, return; otherwise start searching from upper_limit.
        if (status == ZX_OK) {
            ZX_DEBUG_ASSERT(out < upper_limit);
            ZX_DEBUG_ASSERT(!map_.GetOne(out));
            ZX_DEBUG_ASSERT(!swap_in_.GetOne(out));
            return out;
        }

        start = upper_limit;
#else
        return index;
#endif
    }
}

bool Allocator::CheckAllocated(size_t index) const {
    AutoLock lock(&lock_);
    return map_.Get(index, index + 1);
}

size_t Allocator::Allocate(AllocatorPromiseKey, WriteTxn* txn) {
    AutoLock lock(&lock_);
    ZX_DEBUG_ASSERT(reserved_ > 0);
    size_t bitoff_start = FindLocked();

    ZX_ASSERT(map_.SetOne(bitoff_start) == ZX_OK);
    storage_->PersistRange(txn, GetMapDataLocked(), bitoff_start, 1);
    reserved_ -= 1;
    storage_->PersistAllocate(txn, 1);
    first_free_ = bitoff_start + 1;
    return bitoff_start;
}

void Allocator::Unreserve(AllocatorPromiseKey, size_t count) {
    AutoLock lock(&lock_);
#ifdef __Fuchsia__
    ZX_DEBUG_ASSERT(swap_in_.num_bits() == 0);
    ZX_DEBUG_ASSERT(swap_out_.num_bits() == 0);
#endif
    ZX_DEBUG_ASSERT(reserved_ >= count);
    reserved_ -= count;
}

#ifdef __Fuchsia__
size_t Allocator::Swap(AllocatorPromiseKey, size_t old_index) {
    AutoLock lock(&lock_);
    ZX_DEBUG_ASSERT(reserved_ > 0);

    if (old_index > 0) {
        ZX_DEBUG_ASSERT(map_.GetOne(old_index));
        ZX_ASSERT(swap_out_.SetOne(old_index) == ZX_OK);
    }

    size_t new_index = FindLocked();
    ZX_DEBUG_ASSERT(!swap_in_.GetOne(new_index));
    ZX_ASSERT(swap_in_.SetOne(new_index) == ZX_OK);
    reserved_--;
    first_free_ = new_index + 1;
    ZX_DEBUG_ASSERT(swap_in_.num_bits() >= swap_out_.num_bits());
    return new_index;
}

void Allocator::SwapCommit(AllocatorPromiseKey, WriteTxn* txn) {
    AutoLock lock(&lock_);
    if (swap_in_.num_bits() == 0 && swap_out_.num_bits() == 0) {
        return;
    }

    for (auto range = swap_in_.begin(); range != swap_in_.end(); ++range) {
        // Ensure that none of the bits are already allocated.
        ZX_DEBUG_ASSERT(map_.Scan(range->bitoff, range->end(), false));

        // Swap in the new bits.
        zx_status_t status = map_.Set(range->bitoff, range->end());
        ZX_DEBUG_ASSERT(status == ZX_OK);
        storage_->PersistRange(txn, GetMapDataLocked(), range->bitoff, range->bitlen);
    }

    for (auto range = swap_out_.begin(); range != swap_out_.end(); ++range) {
        if (range->bitoff < first_free_) {
            // If we are freeing up a value < our current hint, update hint now.
            first_free_ = range->bitoff;
        }
        // Ensure that all bits are already allocated.
        ZX_DEBUG_ASSERT(map_.Get(range->bitoff, range->end()));

        // Swap out the old bits.
        zx_status_t status = map_.Clear(range->bitoff, range->end());
        ZX_DEBUG_ASSERT(status == ZX_OK);
        storage_->PersistRange(txn, GetMapDataLocked(), range->bitoff, range->bitlen);
    }

    // Update count of allocated blocks.
    // Since we swap out 1 or fewer elements each time one is swapped in,
    // the elements in swap_out can never be greater than those in swap_in.
    ZX_DEBUG_ASSERT(swap_in_.num_bits() >= swap_out_.num_bits());
    storage_->PersistAllocate(txn, swap_in_.num_bits() - swap_out_.num_bits());

    // Clear the reserved/unreserved bitmaps
    swap_in_.ClearAll();
    swap_out_.ClearAll();
}
#endif

#ifdef __Fuchsia__
fbl::Vector<BlockRegion> Allocator::GetAllocatedRegions() const {
    AutoLock lock(&lock_);
    fbl::Vector<BlockRegion> out_regions;
    uint64_t offset = 0;
    uint64_t end = 0;
    while (!map_.Scan(end, map_.size(), false, &offset)) {
        if (map_.Scan(offset, map_.size(), true, &end)) {
            end = map_.size();
        }
        out_regions.push_back({offset, end - offset});
    }
    return out_regions;
}
#endif

} // namespace minfs
