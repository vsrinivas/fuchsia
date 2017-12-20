// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <stdlib.h>
#include <string.h>

#include <bitmap/raw-bitmap.h>

#include <minfs/allocator.h>
#include <minfs/block-txn.h>

#include <utility>

namespace minfs {
namespace {

// Returns the number of blocks necessary to store a pool containing
// |size| bits.
blk_t BitmapBlocksForSize(size_t size) {
    return (static_cast<blk_t>(size) + kMinfsBlockBits - 1) / kMinfsBlockBits;
}

}  // namespace

AllocatorPromise::~AllocatorPromise() {
    ZX_DEBUG_ASSERT(allocator_ != nullptr);

    if (reserved_ > 0) {
        allocator_->Unreserve(reserved_);
    }
}

size_t AllocatorPromise::Allocate(WriteTxn* txn) {
    ZX_DEBUG_ASSERT(allocator_ != nullptr);
    ZX_DEBUG_ASSERT(reserved_ > 0);
    reserved_--;
    return allocator_->Allocate(txn);
}

#ifdef __Fuchsia__
size_t AllocatorPromise::Swap(size_t old_index) {
    ZX_DEBUG_ASSERT(allocator_ != nullptr);
    ZX_DEBUG_ASSERT(reserved_ > 0);
    reserved_--;
    return allocator_->Swap(old_index);
}

void AllocatorPromise::SwapCommit(WriteTxn* txn) {
    ZX_DEBUG_ASSERT(allocator_ != nullptr);
    allocator_->SwapCommit(txn);
}
#endif

AllocatorFvmMetadata::AllocatorFvmMetadata() = default;
AllocatorFvmMetadata::AllocatorFvmMetadata(uint32_t* data_slices,
                                           uint32_t* metadata_slices,
                                           uint64_t slice_size) :
        data_slices_(data_slices), metadata_slices_(metadata_slices),
        slice_size_(slice_size) {}
AllocatorFvmMetadata::AllocatorFvmMetadata(AllocatorFvmMetadata&&) = default;
AllocatorFvmMetadata& AllocatorFvmMetadata::operator=(AllocatorFvmMetadata&&) = default;
AllocatorFvmMetadata::~AllocatorFvmMetadata() = default;

uint32_t AllocatorFvmMetadata::UnitsPerSlices(uint32_t slices, uint32_t unit_size) const {
    uint64_t units = (slice_size_ * slices) / unit_size;
    ZX_DEBUG_ASSERT(units <= std::numeric_limits<uint32_t>::max());
    return static_cast<uint32_t>(units);
}

// NOTE: This helper is only intended to be called for
// values of |blocks| which are known to be convertible to slices
// without loss. This is checked by a runtime assertion.
uint32_t AllocatorFvmMetadata::BlocksToSlices(uint32_t blocks) const {
    const size_t kBlocksPerSlice = slice_size_ / kMinfsBlockSize;
    uint32_t slices = static_cast<uint32_t>(blocks / kBlocksPerSlice);
    ZX_DEBUG_ASSERT(UnitsPerSlices(slices, kMinfsBlockSize) == blocks);
    return slices;
}

AllocatorMetadata::AllocatorMetadata() = default;
AllocatorMetadata::AllocatorMetadata(blk_t data_start_block,
                                     blk_t metadata_start_block, bool using_fvm,
                                     AllocatorFvmMetadata fvm,
                                     uint32_t* pool_used, uint32_t* pool_total) :
    data_start_block_(data_start_block), metadata_start_block_(metadata_start_block),
    using_fvm_(using_fvm), fvm_(std::move(fvm)),
    pool_used_(pool_used), pool_total_(pool_total) {}
AllocatorMetadata::AllocatorMetadata(AllocatorMetadata&&) = default;
AllocatorMetadata& AllocatorMetadata::operator=(AllocatorMetadata&&) = default;
AllocatorMetadata::~AllocatorMetadata() = default;

Allocator::Allocator(Bcache* bc, SuperblockManager* sb, size_t unit_size, GrowHandler grow_cb,
                     AllocatorMetadata metadata) :
    bc_(bc), sb_(sb), unit_size_(unit_size), grow_cb_(std::move(grow_cb)),
    metadata_(std::move(metadata)), reserved_(0), first_free_(0) {}

Allocator::~Allocator() {
#ifdef __Fuchsia__
    ZX_DEBUG_ASSERT(swap_in_.num_bits() == 0);
    ZX_DEBUG_ASSERT(swap_out_.num_bits() == 0);
#endif
}

zx_status_t Allocator::Create(Bcache* bc, SuperblockManager* sb, fs::ReadTxn* txn, size_t unit_size,
                              GrowHandler grow_cb, AllocatorMetadata metadata,
                              fbl::unique_ptr<Allocator>* out) {
    auto allocator = fbl::unique_ptr<Allocator>(new Allocator(bc, sb, unit_size,
                                                              std::move(grow_cb),
                                                              std::move(metadata)));
    blk_t pool_blocks = BitmapBlocksForSize(allocator->metadata_.PoolTotal());

    zx_status_t status;
    if ((status = allocator->map_.Reset(pool_blocks * kMinfsBlockBits)) != ZX_OK) {
        return status;
    }
    if ((status = allocator->map_.Shrink(allocator->metadata_.PoolTotal())) != ZX_OK) {
        return status;
    }
#ifdef __Fuchsia__
    vmoid_t map_vmoid;
    if ((status = bc->AttachVmo(allocator->map_.StorageUnsafe()->GetVmo(), &map_vmoid)) != ZX_OK) {
        return status;
    }
    vmoid_t data = map_vmoid;
#else
    const void* data = allocator->map_.StorageUnsafe()->GetData();
#endif
    txn->Enqueue(data, 0, allocator->metadata_.MetadataStartBlock(), pool_blocks);
    *out = std::move(allocator);
    return ZX_OK;
}

zx_status_t Allocator::Reserve(WriteTxn* txn, size_t count,
                               fbl::unique_ptr<AllocatorPromise>* out_promise) {
    if (GetAvailable() < count) {
        // If we do not have enough free elements, attempt to extend the partition.
        zx_status_t status;
        //TODO(planders): Allow Extend to take in count.
        if ((status = Extend(txn)) != ZX_OK) {
            return status;
        }

        ZX_DEBUG_ASSERT(GetAvailable() >= count);
    }

    reserved_ += count;
    (*out_promise).reset(new AllocatorPromise(this, count));
    return ZX_OK;
}

void Allocator::Free(WriteTxn* txn, size_t index) {
    ZX_DEBUG_ASSERT(map_.Get(index, index + 1));
    map_.Clear(index, index + 1);
    Persist(txn, index, 1);
    metadata_.PoolRelease(1);
    sb_->Write(txn);

    if (index < first_free_) {
        first_free_ = index;
    }
}

size_t Allocator::Find() {
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
            return out;
        }

        start = upper_limit;
#else
        return index;
#endif
    }
}

size_t Allocator::Allocate(WriteTxn* txn) {
    ZX_DEBUG_ASSERT(reserved_ > 0);
    size_t bitoff_start = Find();
    ZX_ASSERT(map_.Set(bitoff_start, bitoff_start + 1) == ZX_OK);
    Persist(txn, bitoff_start, 1);
    metadata_.PoolAllocate(1);
    reserved_ -= 1;
    sb_->Write(txn);
    first_free_ = bitoff_start + 1;
    return bitoff_start;
}

zx_status_t Allocator::Extend(WriteTxn* txn) {
#ifdef __Fuchsia__
    TRACE_DURATION("minfs", "Minfs::Allocator::Extend");
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
        FS_TRACE_ERROR("minfs::Allocator::Extend failed to grow (on disk): %d\n", status);
        return status;
    }

    if (grow_cb_) {
        if ((status = grow_cb_(pool_size)) != ZX_OK) {
            FS_TRACE_ERROR("minfs::Allocator grow callback failure: %d\n", status);
            return status;
        }
    }

    // Extend the in memory representation of our allocation pool -- it grew!
    ZX_DEBUG_ASSERT(pool_size >= map_.size());
    size_t old_pool_size = map_.size();
    if ((status = map_.Grow(fbl::round_up(pool_size, kMinfsBlockBits))) != ZX_OK) {
        FS_TRACE_ERROR("minfs::Allocator failed to Grow (in memory): %d\n", status);
        return ZX_ERR_NO_SPACE;
    }
    // Grow before shrinking to ensure the underlying storage is a multiple
    // of kMinfsBlockSize.
    map_.Shrink(pool_size);

    metadata_.Fvm().SetDataSlices(data_slices_new);
    metadata_.SetPoolTotal(pool_size);
    sb_->Write(txn);

    // Update the block bitmap.
    Persist(txn, old_pool_size, pool_size - old_pool_size);
    return ZX_OK;
#else
    return ZX_ERR_NO_SPACE;
#endif
}

#ifdef __Fuchsia__
size_t Allocator::Swap(size_t old_index) {
    ZX_DEBUG_ASSERT(reserved_ > 0);

    size_t bitoff_start = Find();
    ZX_ASSERT(swap_in_.Set(bitoff_start, bitoff_start + 1) == ZX_OK);
    reserved_--;
    first_free_ = bitoff_start + 1;

    if (old_index > 0) {
        ZX_DEBUG_ASSERT(map_.Get(old_index, old_index + 1));
        ZX_ASSERT(swap_out_.Set(old_index, old_index + 1) == ZX_OK);
    }

    ZX_DEBUG_ASSERT(swap_in_.num_bits() >= swap_out_.num_bits());
    return bitoff_start;
}

void Allocator::SwapCommit(WriteTxn* txn) {
    // No action required if no blocks have been reserved.
    if (!swap_in_.num_bits() && !swap_out_.num_bits()) {
        return;
    }

    ZX_DEBUG_ASSERT(txn != nullptr);

    for (auto range = swap_in_.begin(); range != swap_in_.end(); ++range) {
        // Ensure that none of the bits are already allocated.
        ZX_DEBUG_ASSERT(map_.Scan(range->bitoff, range->end(), false));

        // Swap in the new bits.
        zx_status_t status = map_.Set(range->bitoff, range->end());
        ZX_DEBUG_ASSERT(status == ZX_OK);
        Persist(txn, range->bitoff, range->bitlen);
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
        Persist(txn, range->bitoff, range->bitlen);
    }

    // Update count of allocated blocks.
    // Since we swap out 1 or fewer elements each time one is swapped in,
    // the elements in swap_out can never be greater than those in swap_in.
    ZX_DEBUG_ASSERT(swap_in_.num_bits() >= swap_out_.num_bits());
    metadata_.PoolAllocate(static_cast<blk_t>(swap_in_.num_bits() - swap_out_.num_bits()));
    sb_->Write(txn);

    // Clear the reserved/unreserved bitmaps
    swap_in_.ClearAll();
    swap_out_.ClearAll();
}
#endif

void Allocator::Persist(WriteTxn* txn, size_t index, size_t count) {
    blk_t rel_block = static_cast<blk_t>(index) / kMinfsBlockBits;
    blk_t abs_block = metadata_.MetadataStartBlock() + rel_block;
    blk_t blk_count = BitmapBlocksForSize(count);

#ifdef __Fuchsia__
    zx_handle_t data = map_.StorageUnsafe()->GetVmo().get();
#else
    const void* data = map_.StorageUnsafe()->GetData();
#endif
    txn->Enqueue(data, rel_block, abs_block, blk_count);
}

#ifdef __Fuchsia__
fbl::Vector<BlockRegion> Allocator::GetAllocatedRegions() const {
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

void Allocator::Unreserve(size_t count) {
#ifdef __Fuchsia__
    ZX_DEBUG_ASSERT(swap_in_.num_bits() == 0);
    ZX_DEBUG_ASSERT(swap_out_.num_bits() == 0);
#endif
    ZX_DEBUG_ASSERT(reserved_ >= count);
    reserved_ -= count;
}
} // namespace minfs
