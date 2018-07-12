// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the structure used to allocate
// from an on-disk bitmap.

#pragma once

#include <fbl/function.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>
#include <lib/fzl/mapped-vmo.h>

#include <minfs/format.h>
#include <minfs/block-txn.h>

#include "superblock.h"

namespace minfs {

#ifdef __Fuchsia__
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

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

    void PoolAllocate(uint32_t units) {
        *pool_used_ += units;
    }

    void PoolRelease(uint32_t units) {
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

// The Allocator class is used to abstract away the mechanism by which
// minfs allocates objects internally.
class Allocator {
public:
    // Callback invoked after the data portion of the allocator grows.
    using GrowHandler = fbl::Function<zx_status_t(uint32_t pool_size)>;

    Allocator() = delete;
    DISALLOW_COPY_ASSIGN_AND_MOVE(Allocator);
    ~Allocator();

    // Creates an allocator.
    //
    // |grow_cb| is an optional callback to increase the size of the
    // allocator.
    static zx_status_t Create(Bcache* bc, Superblock* sb, ReadTxn* txn,
                              size_t unit_size, GrowHandler grow_cb,
                              AllocatorMetadata metadata, fbl::unique_ptr<Allocator>* out);

    // Allocate a new item.
    zx_status_t Allocate(WriteTxn* txn, size_t* out_index);

    // Free an item from the allocator.
    void Free(WriteTxn* txn, size_t index);

private:
    friend class MinfsChecker;

    Allocator(Bcache* bc, Superblock* sb, size_t unit_size, GrowHandler grow_cb,
              AllocatorMetadata metadata);

    zx_status_t Extend(WriteTxn* txn);

    // Write back the allocation of the following items to disk.
    void Persist(WriteTxn* txn, size_t index, size_t count);

    Bcache* bc_;
    Superblock* sb_;
    size_t unit_size_;
    GrowHandler grow_cb_;
    AllocatorMetadata metadata_;
    RawBitmap map_;

    // TODO(smklein): Keep a counter of the "reserved but not allocated" blocks
    // here when implementing delayed allocation.
};

} // namespace minfs
