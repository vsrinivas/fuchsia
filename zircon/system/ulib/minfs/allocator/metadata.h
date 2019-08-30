// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bitmap/storage.h>
#include <fbl/function.h>
#include <fbl/macros.h>
#include <minfs/format.h>
#include <minfs/superblock.h>

namespace minfs {

// Represents the FVM-related information for the allocator, including
// slice usage and a mechanism to grow the allocation pool.
class AllocatorFvmMetadata {
 public:
  AllocatorFvmMetadata();
  AllocatorFvmMetadata(uint32_t* data_slices, uint32_t* metadata_slices, uint64_t slice_size);
  AllocatorFvmMetadata(AllocatorFvmMetadata&&);
  AllocatorFvmMetadata& operator=(AllocatorFvmMetadata&&);
  ~AllocatorFvmMetadata();

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AllocatorFvmMetadata);

  uint32_t UnitsPerSlices(uint32_t slice, uint32_t unit_size) const;
  uint32_t SlicesToBlocks(uint32_t slices) const;
  uint32_t BlocksToSlices(uint32_t blocks) const;

  uint32_t DataSlices() const { return *data_slices_; }

  void SetDataSlices(uint32_t slices) { *data_slices_ = slices; }

  uint32_t MetadataSlices() const { return *metadata_slices_; }

  void SetMetadataSlices(uint32_t slices) { *metadata_slices_ = slices; }

  uint64_t SliceSize() const { return slice_size_; }

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

  blk_t DataStartBlock() const { return data_start_block_; }

  blk_t MetadataStartBlock() const { return metadata_start_block_; }

  bool UsingFvm() const { return using_fvm_; }

  AllocatorFvmMetadata& Fvm() {
    ZX_DEBUG_ASSERT(UsingFvm());
    return fvm_;
  }

  uint32_t PoolUsed() const { return *pool_used_; }

  // Return the number of elements which are still available for allocation/reservation.
  uint32_t PoolAvailable() const { return *pool_total_ - *pool_used_; }

  void PoolAllocate(uint32_t units) {
    ZX_DEBUG_ASSERT(*pool_used_ + units <= *pool_total_);
    *pool_used_ += units;
  }

  void PoolRelease(uint32_t units) {
    ZX_DEBUG_ASSERT(*pool_used_ >= units);
    *pool_used_ -= units;
  }

  uint32_t PoolTotal() const { return *pool_total_; }

  void SetPoolTotal(uint32_t total) { *pool_total_ = total; }

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

}  // namespace minfs
