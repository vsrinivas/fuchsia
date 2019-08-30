// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "metadata.h"

namespace minfs {

AllocatorFvmMetadata::AllocatorFvmMetadata() = default;
AllocatorFvmMetadata::AllocatorFvmMetadata(uint32_t* data_slices, uint32_t* metadata_slices,
                                           uint64_t slice_size)
    : data_slices_(data_slices), metadata_slices_(metadata_slices), slice_size_(slice_size) {}
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
AllocatorMetadata::AllocatorMetadata(blk_t data_start_block, blk_t metadata_start_block,
                                     bool using_fvm, AllocatorFvmMetadata fvm, uint32_t* pool_used,
                                     uint32_t* pool_total)
    : data_start_block_(data_start_block),
      metadata_start_block_(metadata_start_block),
      using_fvm_(using_fvm),
      fvm_(std::move(fvm)),
      pool_used_(pool_used),
      pool_total_(pool_total) {}
AllocatorMetadata::AllocatorMetadata(AllocatorMetadata&&) = default;
AllocatorMetadata& AllocatorMetadata::operator=(AllocatorMetadata&&) = default;
AllocatorMetadata::~AllocatorMetadata() = default;

}  // namespace minfs
