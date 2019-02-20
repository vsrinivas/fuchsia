// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <mutex>
#include <utility>

#include <stdlib.h>
#include <string.h>

#include <bitmap/raw-bitmap.h>
#include <minfs/allocator.h>
#include <minfs/block-txn.h>

namespace minfs {
namespace {

// Returns the number of blocks necessary to store a pool containing
// |size| bits.
blk_t BitmapBlocksForSize(size_t size) {
    return (static_cast<blk_t>(size) + kMinfsBlockBits - 1) / kMinfsBlockBits;
}

}  // namespace

AllocatorPromise::~AllocatorPromise() {
    Cancel();
}

zx_status_t AllocatorPromise::Initialize(WriteTxn* txn, size_t reserved, Allocator* allocator) {
    if (allocator_ != nullptr) {
        return ZX_ERR_BAD_STATE;
    }

    if (allocator == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    ZX_DEBUG_ASSERT(reserved_ == 0);

    zx_status_t status = allocator->Reserve({}, txn, reserved, this);
    if (status == ZX_OK) {
        allocator_ = allocator;
        reserved_ = reserved;
    }
    return status;
}

size_t AllocatorPromise::Allocate(WriteTxn* txn) {
    ZX_DEBUG_ASSERT(allocator_ != nullptr);
    ZX_DEBUG_ASSERT(reserved_ > 0);
    reserved_--;
    return allocator_->Allocate({}, txn);
}

#ifdef __Fuchsia__
size_t AllocatorPromise::Swap(size_t old_index) {
    ZX_DEBUG_ASSERT(allocator_ != nullptr);
    ZX_DEBUG_ASSERT(reserved_ > 0);
    reserved_--;
    return allocator_->Swap({}, old_index);
}

void AllocatorPromise::SwapCommit(WriteTxn* txn) {
    ZX_DEBUG_ASSERT(allocator_ != nullptr);
    allocator_->SwapCommit({}, txn);
}

void AllocatorPromise::Split(size_t requested, AllocatorPromise* other_promise) {
    ZX_DEBUG_ASSERT(requested <= reserved_);
    ZX_DEBUG_ASSERT(other_promise != nullptr);

    if (other_promise->IsInitialized()) {
        ZX_DEBUG_ASSERT(other_promise->allocator_ == allocator_);
    } else {
        other_promise->allocator_ = allocator_;
    }

    reserved_ -= requested;
    other_promise->reserved_ += requested;
}

#endif

void AllocatorPromise::Cancel() {
    if (IsInitialized() && reserved_ > 0) {
        allocator_->Unreserve({}, reserved_);
        reserved_ = 0;
    }

    ZX_DEBUG_ASSERT(reserved_ == 0);
}

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

Allocator::~Allocator() {
#ifdef __Fuchsia__
    AutoLock lock(&lock_);
    ZX_DEBUG_ASSERT(swap_in_.num_bits() == 0);
    ZX_DEBUG_ASSERT(swap_out_.num_bits() == 0);
#endif
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

PersistentStorage::PersistentStorage(Bcache* bc, SuperblockManager* sb, size_t unit_size,
                                     GrowHandler grow_cb, AllocatorMetadata metadata) :
#ifdef __Fuchsia__
      bc_(bc), unit_size_(unit_size),
#endif
      sb_(sb),  grow_cb_(std::move(grow_cb)), metadata_(std::move(metadata)) {}

zx_status_t Allocator::Create(fs::ReadTxn* txn, fbl::unique_ptr<AllocatorStorage> storage,
                              fbl::unique_ptr<Allocator>* out) FS_TA_NO_THREAD_SAFETY_ANALYSIS {
    // Ignore thread-safety analysis on the |allocator| object; no one has an
    // external reference to it yet.
    zx_status_t status;
    fbl::unique_ptr<Allocator> allocator(new Allocator(std::move(storage)));

    blk_t total_blocks = allocator->storage_->PoolTotal();
    blk_t pool_blocks = BitmapBlocksForSize(total_blocks);
    if ((status = allocator->map_.Reset(pool_blocks * kMinfsBlockBits)) != ZX_OK) {
        return status;
    }
    if ((status = allocator->map_.Shrink(total_blocks)) != ZX_OK) {
        return status;
    }

#ifdef __Fuchsia__
    vmoid_t map_vmoid;
    if ((status = allocator->storage_->AttachVmo(allocator->map_.StorageUnsafe()->GetVmo(),
                                                 &map_vmoid)) != ZX_OK) {
        return status;
    }
    allocator->storage_->Load(txn, map_vmoid);
#else
    allocator->storage_->Load(txn, allocator->GetMapDataLocked());
#endif
    *out = std::move(allocator);
    return ZX_OK;
}

#ifdef __Fuchsia__
zx_status_t PersistentStorage::AttachVmo(const zx::vmo& vmo, vmoid_t* vmoid) {
    return bc_->AttachVmo(vmo, vmoid);
}
#endif

void PersistentStorage::Load(fs::ReadTxn* txn, ReadData data) {
    blk_t pool_blocks = BitmapBlocksForSize(metadata_.PoolTotal());
    txn->Enqueue(data, 0, metadata_.MetadataStartBlock(), pool_blocks);
}

zx_status_t PersistentStorage::Extend(WriteTxn* txn, WriteData data, GrowMapCallback grow_map) {
#ifdef __Fuchsia__
    TRACE_DURATION("minfs", "Minfs::Allocator::Extend");
    ZX_DEBUG_ASSERT(txn != nullptr);
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
    size_t old_pool_size;
    if ((status = grow_map(pool_size, &old_pool_size)) != ZX_OK) {
        return status;
    }

    metadata_.Fvm().SetDataSlices(data_slices_new);
    metadata_.SetPoolTotal(pool_size);
    sb_->Write(txn);

    // Update the block bitmap.
    PersistRange(txn, data, old_pool_size, pool_size - old_pool_size);
    return ZX_OK;
#else
    return ZX_ERR_NO_SPACE;
#endif
}

void PersistentStorage::PersistRange(WriteTxn* txn, WriteData data, size_t index, size_t count) {
    ZX_DEBUG_ASSERT(txn != nullptr);
    // Determine the blocks containing the first and last indices.
    blk_t first_rel_block = static_cast<blk_t>(index / kMinfsBlockBits);
    blk_t last_rel_block = static_cast<blk_t>((index + count - 1) / kMinfsBlockBits);

    // Calculate number of blocks based on the first and last blocks touched.
    blk_t block_count = last_rel_block - first_rel_block + 1;

    blk_t abs_block = metadata_.MetadataStartBlock() + first_rel_block;
    txn->Enqueue(data, first_rel_block, abs_block, block_count);
}

void PersistentStorage::PersistAllocate(WriteTxn* txn, size_t count) {
    ZX_DEBUG_ASSERT(txn != nullptr);
    metadata_.PoolAllocate(static_cast<blk_t>(count));
    sb_->Write(txn);
}

void PersistentStorage::PersistRelease(WriteTxn* txn, size_t count) {
    ZX_DEBUG_ASSERT(txn != nullptr);
    metadata_.PoolRelease(static_cast<blk_t>(count));
    sb_->Write(txn);
}

} // namespace minfs
