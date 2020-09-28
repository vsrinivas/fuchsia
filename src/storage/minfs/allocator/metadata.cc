// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/allocator/metadata.h"

#include <utility>

namespace minfs {

uint32_t AllocatorFvmMetadata::UnitsPerSlices(uint32_t slices, uint32_t unit_size) const {
  uint64_t units = (SliceSize() * slices) / unit_size;
  ZX_DEBUG_ASSERT(units <= std::numeric_limits<uint32_t>::max());
  return static_cast<uint32_t>(units);
}

// NOTE: This helper is only intended to be called for
// values of |blocks| which are known to be convertible to slices
// without loss. This is checked by a runtime assertion.
uint32_t AllocatorFvmMetadata::BlocksToSlices(uint32_t blocks) const {
  const size_t kBlocksPerSlice = SliceSize() / superblock_->BlockSize();
  uint32_t slices = static_cast<uint32_t>(blocks / kBlocksPerSlice);
  ZX_DEBUG_ASSERT(UnitsPerSlices(slices, superblock_->BlockSize()) == blocks);
  return slices;
}

AllocatorMetadata::AllocatorMetadata(blk_t data_start_block, blk_t metadata_start_block,
                                     bool using_fvm, AllocatorFvmMetadata fvm,
                                     SuperblockManager* superblock,
                                     SuperblockAllocatorAccess access)
    : data_start_block_(data_start_block),
      metadata_start_block_(metadata_start_block),
      using_fvm_(using_fvm),
      fvm_(std::move(fvm)),
      superblock_(superblock),
      superblock_access_(access) {}

}  // namespace minfs
