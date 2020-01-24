// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "allocator.h"

#include <stdlib.h>
#include <string.h>

#include <limits>
#include <utility>

#include <bitmap/raw-bitmap.h>

namespace minfs {

Allocator::~Allocator() {
  AutoLock lock(&lock_);
  ZX_DEBUG_ASSERT(swap_in_.num_bits() == 0);
  ZX_DEBUG_ASSERT(swap_out_.num_bits() == 0);
}

zx_status_t Allocator::LoadStorage(fs::ReadTxn* txn) {
  AutoLock lock(&lock_);
  fuchsia_hardware_block_VmoId map_vmoid;
  zx_status_t status = storage_->AttachVmo(map_.StorageUnsafe()->GetVmo(), &map_vmoid);
  if (status != ZX_OK) {
    return status;
  }
  storage_->Load(txn, map_vmoid.id);
  return ZX_OK;
}

size_t Allocator::GetAvailableLocked() const {
  size_t total_reserved = reserved_ + swap_in_.num_bits();
  ZX_DEBUG_ASSERT(storage_->PoolAvailable() >= total_reserved);
  return storage_->PoolAvailable() - total_reserved;
}

WriteData Allocator::GetMapDataLocked() const { return map_.StorageUnsafe()->GetVmo().get(); }

size_t Allocator::FindLocked() const {
  ZX_DEBUG_ASSERT(reserved_ > 0);
  size_t start = first_free_;

  while (true) {
    // Search for first free element in the map.
    size_t index;
    ZX_ASSERT(map_.Find(false, start, map_.size(), 1, &index) == ZX_OK);

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
  }
}

size_t Allocator::Swap(AllocatorReservationKey, size_t old_index) {
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

void Allocator::SwapCommit(AllocatorReservationKey, PendingWork* transaction) {
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
    storage_->PersistRange(transaction, GetMapDataLocked(), range->bitoff, range->bitlen);
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
    storage_->PersistRange(transaction, GetMapDataLocked(), range->bitoff, range->bitlen);
  }

  // Update count of allocated blocks.
  // Since we swap out 1 or fewer elements each time one is swapped in,
  // the elements in swap_out can never be greater than those in swap_in.
  ZX_DEBUG_ASSERT(swap_in_.num_bits() >= swap_out_.num_bits());
  storage_->PersistAllocate(transaction, swap_in_.num_bits() - swap_out_.num_bits());

  // Clear the reserved/unreserved bitmaps
  swap_in_.ClearAll();
  swap_out_.ClearAll();
}

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

}  // namespace minfs
