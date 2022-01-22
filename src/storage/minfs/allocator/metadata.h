// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_ALLOCATOR_METADATA_H_
#define SRC_STORAGE_MINFS_ALLOCATOR_METADATA_H_

#include <lib/fit/function.h>

#include <bitmap/storage.h>
#include <fbl/macros.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/superblock.h"

namespace minfs {

// This structure contains pointers to relevant allocator fields in the superblock.
struct SuperblockAllocatorAccess {
  uint32_t Superblock::*used;
  uint32_t Superblock::*total;
  uint32_t Superblock::*data_slices;
  uint32_t Superblock::*metadata_slices;

  // Returns pointers suitable for the block allocator.
  static SuperblockAllocatorAccess Blocks() {
    return SuperblockAllocatorAccess{&Superblock::alloc_block_count, &Superblock::block_count,
                                     &Superblock::dat_slices, &Superblock::abm_slices};
  }

  // Returns pointers suitable for the inode allocator.
  static SuperblockAllocatorAccess Inodes() {
    return SuperblockAllocatorAccess{&Superblock::alloc_inode_count, &Superblock::inode_count,
                                     &Superblock::ino_slices, &Superblock::ibm_slices};
  }
};

// Represents the FVM-related information for the allocator, including
// slice usage and a mechanism to grow the allocation pool.
class AllocatorFvmMetadata {
 public:
  AllocatorFvmMetadata() = default;
  AllocatorFvmMetadata(SuperblockManager* superblock, SuperblockAllocatorAccess access)
      : superblock_(superblock), superblock_access_(access) {}

  // Movable, not copyable.
  AllocatorFvmMetadata(AllocatorFvmMetadata&&) = default;
  AllocatorFvmMetadata& operator=(AllocatorFvmMetadata&&) = default;

  uint32_t UnitsPerSlices(uint32_t slice, uint32_t unit_size) const;
  uint32_t SlicesToBlocks(uint32_t slices) const;
  uint32_t BlocksToSlices(uint32_t blocks) const;

  uint32_t DataSlices() const { return superblock_->Info().*superblock_access_.data_slices; }

  void SetDataSlices(uint32_t slices) {
    superblock_->MutableInfo()->*superblock_access_.data_slices = slices;
  }

  uint32_t MetadataSlices() const {
    return superblock_->Info().*superblock_access_.metadata_slices;
  }

  void SetMetadataSlices(uint32_t slices) {
    superblock_->MutableInfo()->*superblock_access_.metadata_slices = slices;
  }

  uint64_t SliceSize() const { return superblock_->Info().slice_size; }

 private:
  SuperblockManager* superblock_ = nullptr;
  SuperblockAllocatorAccess superblock_access_ = {};
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
  AllocatorMetadata() = default;
  AllocatorMetadata(blk_t data_start_block, blk_t metadata_start_block, bool using_fvm,
                    AllocatorFvmMetadata fvm, SuperblockManager* superblock_manager,
                    SuperblockAllocatorAccess interface);

  // Movable, not copyable.
  AllocatorMetadata(AllocatorMetadata&&) = default;
  AllocatorMetadata& operator=(AllocatorMetadata&&) = default;

  blk_t DataStartBlock() const { return data_start_block_; }

  blk_t MetadataStartBlock() const { return metadata_start_block_; }

  bool UsingFvm() const { return using_fvm_; }

  AllocatorFvmMetadata& Fvm() {
    ZX_DEBUG_ASSERT(UsingFvm());
    return fvm_;
  }

  uint32_t PoolUsed() const { return superblock_->Info().*superblock_access_.used; }

  // Return the number of elements which are still available for allocation/reservation.
  uint32_t PoolAvailable() const { return PoolTotal() - PoolUsed(); }

  void PoolAllocate(uint32_t units) {
    ZX_DEBUG_ASSERT_MSG(PoolTotal() - PoolUsed() >= units, "total=%u, used=%u, units=%u\n",
                        PoolTotal(), PoolUsed(), units);
    superblock_->MutableInfo()->*superblock_access_.used += units;
  }

  void PoolRelease(uint32_t units) {
    ZX_DEBUG_ASSERT_MSG(PoolUsed() >= units, "used=%u, units=%u\n", PoolUsed(), units);
    superblock_->MutableInfo()->*superblock_access_.used -= units;
  }

  uint32_t PoolTotal() const { return superblock_->Info().*superblock_access_.total; }

  void SetPoolTotal(uint32_t total) {
    superblock_->MutableInfo()->*superblock_access_.total = total;
  }

 private:
  // Block at which data for the allocator starts.
  blk_t data_start_block_;

  // Block at which metadata for the allocator starts.
  blk_t metadata_start_block_;

  // This metadata is only valid if the Allocator is using an FVM.
  bool using_fvm_;
  AllocatorFvmMetadata fvm_;

  SuperblockManager* superblock_ = nullptr;
  SuperblockAllocatorAccess superblock_access_ = {};
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_ALLOCATOR_METADATA_H_
