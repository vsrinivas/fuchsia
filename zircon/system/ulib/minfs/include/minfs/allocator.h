// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the structure used to allocate
// from an on-disk bitmap.

#pragma once

#include <bitmap/raw-bitmap.h>
#include <bitmap/rle-bitmap.h>
#include <bitmap/storage.h>

#include <fbl/function.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>

#ifdef __Fuchsia__
#include <fuchsia/minfs/c/fidl.h>
#endif

#include <minfs/block-txn.h>
#include <minfs/format.h>
#include <minfs/mutex.h>
#include <minfs/superblock.h>

namespace minfs {
#ifdef __Fuchsia__
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
using BlockRegion = fuchsia_minfs_BlockRegion;
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

class Allocator;

// This class represents a promise from an Allocator to save a particular number of reserved
// elements for later allocation. Allocation for reserved elements must be done through the
// AllocatorPromise class.
// This class is thread-compatible.
// This class is not assignable, copyable, or moveable.
class AllocatorPromise {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(AllocatorPromise);

    AllocatorPromise() {}
    ~AllocatorPromise();

    // Returns |ZX_OK| when |allocator| reserves |reserved| elements and |this| is successfully
    // initialized. Returns an error if not enough elements are available for reservation,
    // |allocator| is null, or |this| was previously initialized.
    zx_status_t Initialize(WriteTxn* txn, size_t reserved, Allocator* allocator);

    bool IsInitialized() const { return allocator_ != nullptr; }

    // Allocate a new item in allocator_. Return the index of the newly allocated item.
    // A call to Allocate() is effectively the same as a call to Swap(0) + SwapCommit(), but under
    // the hood completes these operations more efficiently as additional state doesn't need to be
    // stored between the two.
    size_t Allocate(WriteTxn* txn);

    // Unreserve all currently reserved items.
    void Cancel();

#ifdef __Fuchsia__
    // Swap the element currently allocated at |old_index| for a new index.
    // If |old_index| is 0, a new block will still be allocated, but no blocks will be de-allocated.
    // The swap will not be persisted until a call to SwapCommit is made.
    size_t Swap(size_t old_index);

    // Commit any pending swaps, allocating new indices and de-allocating old indices.
    void SwapCommit(WriteTxn* txn);

    // Remove |requested| reserved elements and give them to |other_promise|.
    // The reserved count belonging to the Allocator does not change.
    void Split(size_t requested, AllocatorPromise* other_promise);

    size_t GetReserved() const { return reserved_; }
#endif
private:
    Allocator* allocator_ = nullptr;
    size_t reserved_ = 0;

    // TODO(planders): Optionally store swap info in AllocatorPromise,
    //                 to ensure we only swap the current promise's blocks on SwapCommit.
};

// An empty key class which represents the |AllocatorPromise|'s access to
// restricted |Allocator| interfaces.
class AllocatorPromiseKey {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(AllocatorPromiseKey);
private:
    friend AllocatorPromise;
    AllocatorPromiseKey() {}
};

// Represents the FVM-related information for the allocator, including
// slice usage and a mechanism to grow the allocation pool.
class AllocatorFvmMetadata {
public:
    AllocatorFvmMetadata();
    AllocatorFvmMetadata(uint32_t* data_slices, uint32_t* metadata_slices,
                         uint64_t slice_size);
    AllocatorFvmMetadata(AllocatorFvmMetadata&&);
    AllocatorFvmMetadata& operator=(AllocatorFvmMetadata&&);
    ~AllocatorFvmMetadata();

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AllocatorFvmMetadata);

    uint32_t UnitsPerSlices(uint32_t slice, uint32_t unit_size) const;
    uint32_t SlicesToBlocks(uint32_t slices) const;
    uint32_t BlocksToSlices(uint32_t blocks) const;

    uint32_t DataSlices() const {
        return *data_slices_;
    }

    void SetDataSlices(uint32_t slices) {
        *data_slices_ = slices;
    }

    uint32_t MetadataSlices() const {
        return *metadata_slices_;
    }

    void SetMetadataSlices(uint32_t slices) {
        *metadata_slices_ = slices;
    }

    uint64_t SliceSize() const {
        return slice_size_;
    }

private:
    // Slices used by the allocator's data.
    uint32_t* data_slices_;
    // Slices used by the allocator's metadata.
    uint32_t* metadata_slices_;
    // Constant slice size used by FVM.
    uint64_t slice_size_;
};

// Metadata information used to initialize a generic allocator.
//
// This structure contains references to the global superblock,
// for fields that are intended to be updated.
//
// The allocator is the sole mutator of these fields while the
// filesystem is mounted.
class AllocatorMetadata {
public:
    AllocatorMetadata();
    AllocatorMetadata(blk_t data_start_block, blk_t metadata_start_block, bool using_fvm,
                      AllocatorFvmMetadata fvm, uint32_t* pool_used, uint32_t* pool_total);
    AllocatorMetadata(AllocatorMetadata&&);
    AllocatorMetadata& operator=(AllocatorMetadata&&);
    ~AllocatorMetadata();
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AllocatorMetadata);

    blk_t DataStartBlock() const {
        return data_start_block_;
    }

    blk_t MetadataStartBlock() const {
        return metadata_start_block_;
    }

    bool UsingFvm() const {
        return using_fvm_;
    }

    AllocatorFvmMetadata& Fvm() {
        ZX_DEBUG_ASSERT(UsingFvm());
        return fvm_;
    }

    uint32_t PoolUsed() const {
        return *pool_used_;
    }

    // Return the number of elements which are still available for allocation/reservation.
    uint32_t PoolAvailable() const {
        return *pool_total_ - *pool_used_;
    }

    void PoolAllocate(uint32_t units) {
        ZX_DEBUG_ASSERT(*pool_used_ + units <= *pool_total_);
        *pool_used_ += units;
    }

    void PoolRelease(uint32_t units) {
        ZX_DEBUG_ASSERT(*pool_used_ >= units);
        *pool_used_ -= units;
    }

    uint32_t PoolTotal() const {
        return *pool_total_;
    }

    void SetPoolTotal(uint32_t total) {
        *pool_total_ = total;
    }

private:
    // Block at which data for the allocator starts.
    blk_t data_start_block_;

    // Block at which metadata for the allocator starts.
    blk_t metadata_start_block_;

    // This metadata is only valid if the Allocator is using an FVM.
    bool using_fvm_;
    AllocatorFvmMetadata fvm_;

    // This information should be re-derivable from the allocator,
    // but is typically stored in the superblock to make mounting
    // faster.
    uint32_t* pool_used_;
    uint32_t* pool_total_;
};

// Types of data to use with read and write transactions.
#ifdef __Fuchsia__
using ReadData = vmoid_t;
using WriteData = zx_handle_t;
#else
using ReadData = const void*;
using WriteData = const void*;
#endif

using GrowMapCallback = fbl::Function<zx_status_t(size_t pool_size, size_t* old_pool_size)>;

// Interface for an Allocator's underlying storage.
class AllocatorStorage {
public:
    AllocatorStorage() = default;
    AllocatorStorage(const AllocatorStorage&) = delete;
    AllocatorStorage& operator=(const AllocatorStorage&) = delete;
    virtual ~AllocatorStorage() {}

#ifdef __Fuchsia__
    virtual zx_status_t AttachVmo(const zx::vmo& vmo, fuchsia_hardware_block_VmoID* vmoid) = 0;
#endif

    // Loads data from disk into |data| using |txn|.
    virtual void Load(fs::ReadTxn* txn, ReadData data) = 0;

    // Extend the on-disk extent containing map_.
    virtual zx_status_t Extend(WriteTxn* txn, WriteData data, GrowMapCallback grow_map) = 0;

    // Returns the number of unallocated elements.
    virtual uint32_t PoolAvailable() const = 0;

    // Returns the total number of elements.
    virtual uint32_t PoolTotal() const = 0;

    // Persists the map at range |index| - |index + count|.
    virtual void PersistRange(WriteTxn* txn, WriteData data, size_t index, size_t count) = 0;

    // Marks |count| elements allocated and persists the latest data.
    virtual void PersistAllocate(WriteTxn* txn, size_t count) = 0;

    // Marks |count| elements released and persists the latest data.
    virtual void PersistRelease(WriteTxn* txn, size_t count) = 0;
};

// A type of storage which represents a persistent disk.
class PersistentStorage : public AllocatorStorage {
public:
    // Callback invoked after the data portion of the allocator grows.
    using GrowHandler = fbl::Function<zx_status_t(uint32_t pool_size)>;

    PersistentStorage() = delete;
    PersistentStorage(const PersistentStorage&) = delete;
    PersistentStorage& operator=(const PersistentStorage&) = delete;

    // |grow_cb| is an optional callback to increase the size of the allocator.
    PersistentStorage(Bcache* bc, SuperblockManager* sb, size_t unit_size, GrowHandler grow_cb,
                      AllocatorMetadata metadata);
    ~PersistentStorage() {}

#ifdef __Fuchsia__
    zx_status_t AttachVmo(const zx::vmo& vmo, fuchsia_hardware_block_VmoID* vmoid);
#endif

    void Load(fs::ReadTxn* txn, ReadData data);

    zx_status_t Extend(WriteTxn* txn, WriteData data, GrowMapCallback grow_map) final;

    uint32_t PoolAvailable() const final { return metadata_.PoolAvailable(); }

    uint32_t PoolTotal() const final { return metadata_.PoolTotal(); }

    void PersistRange(WriteTxn* txn, WriteData data, size_t index, size_t count) final;

    void PersistAllocate(WriteTxn* txn, size_t count) final;

    void PersistRelease(WriteTxn* txn, size_t count) final;
private:
#ifdef __Fuchsia__
    Bcache* bc_;
    size_t unit_size_;
#endif
    SuperblockManager* sb_;
    GrowHandler grow_cb_;
    AllocatorMetadata metadata_;
};

// The Allocator class is used to abstract away the mechanism by which minfs
// allocates objects internally.
//
// This class is thread-safe. However, it is worth pointing out a peculiarity
// regarding |WriteTxn|: This class enqueues operations to a caller-supplied
// WriteTxn as they are necessary, but the source of these enqueued buffers may
// change immediately after |Enqueue()| completes. If a caller delays writeback,
// it is their responsibility to ensure no concurrent mutable methods of
// Allocator are accessed while Transacting the |WriteTxn|, as these methods
// may put the buffer-to-be-written in an inconsistent state.
class Allocator {
public:
    virtual ~Allocator();

    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;

    static zx_status_t Create(fs::ReadTxn* txn, fbl::unique_ptr<AllocatorStorage> storage,
                              fbl::unique_ptr<Allocator>* out);

    // Return the number of total available elements, after taking reservations into account.
    size_t GetAvailable() const FS_TA_EXCLUDES(lock_);

    // Free an item from the allocator.
    void Free(WriteTxn* txn, size_t index) FS_TA_EXCLUDES(lock_);

#ifdef __Fuchsia__
    // Extract a vector of all currently allocated regions in the filesystem.
    fbl::Vector<BlockRegion> GetAllocatedRegions() const FS_TA_EXCLUDES(lock_);
#endif

    // Returns |true| if |index| is allocated. Returns |false| otherwise.
    bool CheckAllocated(size_t index) const FS_TA_EXCLUDES(lock_);

    // AllocatorPromise Methods:
    //
    // The following methods are restricted to AllocatorPromise via the passkey
    // idiom. They are public, but require an empty |AllocatorPromiseKey|.

    // Allocate a single element and return its newly allocated index.
    size_t Allocate(AllocatorPromiseKey, WriteTxn* txn) FS_TA_EXCLUDES(lock_);

    // Reserve |count| elements. This is required in order to later allocate them.
    // Outputs a |promise| which contains reservation details.
    zx_status_t Reserve(AllocatorPromiseKey, WriteTxn* txn, size_t count,
                        AllocatorPromise* promise) FS_TA_EXCLUDES(lock_);

    // Unreserve |count| elements. This may be called in the event of failure, or if we
    // over-reserved initially.
    //
    // PRECONDITION: AllocatorPromise must have |reserved| > 0.
    void Unreserve(AllocatorPromiseKey, size_t count) FS_TA_EXCLUDES(lock_);

#ifdef __Fuchsia__
    // Mark |index| for de-allocation by adding it to the swap_out map,
    // and return the index of a new element to be swapped in.
    // This is currently only used for the block allocator.
    //
    // PRECONDITION: |index| must be allocated in the internal map.
    // PRECONDITION: AllocatorPromise must have |reserved| > 0.
    size_t Swap(AllocatorPromiseKey, size_t index) FS_TA_EXCLUDES(lock_);

    // Allocate / de-allocate elements from the swap_in / swap_out maps (respectively).
    // This persists the results of |Swap|.
    //
    // Since elements are only ever swapped synchronously, all elements represented in the swap_in_
    // and swap_out_ maps are guaranteed to belong to only one Vnode. This method should only be
    // called in the same thread as the block swaps -- i.e. we should never be resolving blocks for
    // more than one vnode at a time.
    void SwapCommit(AllocatorPromiseKey, WriteTxn* txn) FS_TA_EXCLUDES(lock_);
#endif

private:
    Allocator(fbl::unique_ptr<AllocatorStorage> storage) : reserved_(0), first_free_(0),
                                                           storage_(std::move(storage)) {}

    // See |GetAvailable()|.
    size_t GetAvailableLocked() const FS_TA_REQUIRES(lock_);

    // Grows the map to |new_size|, returning the current size as |old_size|.
    zx_status_t GrowMapLocked(size_t new_size, size_t* old_size) FS_TA_REQUIRES(lock_);

    // Acquire direct access to the underlying map storage.
    WriteData GetMapDataLocked() const FS_TA_REQUIRES(lock_);

    // Find and return a free element. This should only be called when reserved_ > 0,
    // ensuring that at least one free element must exist.
    size_t FindLocked() const FS_TA_REQUIRES(lock_);

    // Protects the allocator's metadata.
    // Does NOT guard the allocator |storage_|.
    mutable Mutex lock_;

    // Total number of elements reserved by AllocatorPromise objects. Represents the maximum number
    // of elements that are allowed to be allocated or swapped in at a given time.
    // Once an element is marked for allocation or swap, the reserved_ count is updated accordingly.
    // Remaining reserved blocks will be committed by the end of each Vnode operation,
    // with the exception of copy-on-write data blocks.
    // These will be committed asynchronously via the DataBlockAssigner thread.
    // This means that at the time of reservation if |reserved_| > 0, all reserved blocks must
    // belong to vnodes which are already enqueued in the DataBlockAssigner thread.
    size_t reserved_ FS_TA_GUARDED(lock_);

    // Index of the first free element in the map.
    size_t first_free_ FS_TA_GUARDED(lock_);

    // Represents the Allocator's backing storage.
    fbl::unique_ptr<AllocatorStorage> storage_;
    // A bitmap interface into |storage_|.
    RawBitmap map_ FS_TA_GUARDED(lock_);

#ifdef __Fuchsia__
    // Bitmap of elements to be allocated on SwapCommit.
    bitmap::RleBitmap swap_in_ FS_TA_GUARDED(lock_);
    // Bitmap of elements to be de-allocated on SwapCommit.
    bitmap::RleBitmap swap_out_ FS_TA_GUARDED(lock_);
#endif
};
} // namespace minfs
