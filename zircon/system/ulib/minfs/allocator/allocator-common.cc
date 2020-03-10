// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <limits>
#include <memory>
#include <utility>

#include <bitmap/raw-bitmap.h>

#include "allocator.h"

namespace minfs {

// Static.
zx_status_t Allocator::Create(fs::BufferedOperationsBuilder* builder,
                              std::unique_ptr<AllocatorStorage> storage,
                              std::unique_ptr<Allocator>* out) FS_TA_NO_THREAD_SAFETY_ANALYSIS {
  // Ignore thread-safety analysis on the |allocator| object; no one has an
  // external reference to it yet.
  zx_status_t status;
  std::unique_ptr<Allocator> allocator(new Allocator(std::move(storage)));

  blk_t total_blocks = allocator->storage_->PoolTotal();
  blk_t pool_blocks = allocator->storage_->PoolBlocks();
  if ((status = allocator->map_.Reset(pool_blocks * kMinfsBlockBits)) != ZX_OK) {
    return status;
  }
  if ((status = allocator->map_.Shrink(total_blocks)) != ZX_OK) {
    return status;
  }

  status = allocator->LoadStorage(builder);
  if (status != ZX_OK) {
    return status;
  }

  *out = std::move(allocator);
  return ZX_OK;
}

size_t Allocator::GetAvailable() const {
  AutoLock lock(&lock_);
  return GetAvailableLocked();
}

void Allocator::Free(PendingWork* transaction, size_t index) {
  AutoLock lock(&lock_);
#ifdef __Fuchsia__
  ZX_DEBUG_ASSERT(!swap_out_.GetOne(index));
#endif
  ZX_DEBUG_ASSERT(map_.GetOne(index));

  map_.ClearOne(index);
  storage_->PersistRange(transaction, GetMapDataLocked(), index, 1);
  storage_->PersistRelease(transaction, 1);

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

zx_status_t Allocator::Reserve(AllocatorReservationKey, PendingWork* transaction, size_t count,
                               AllocatorReservation* reservation) {
  AutoLock lock(&lock_);
  if (GetAvailableLocked() < count) {
    // If we do not have enough free elements, attempt to extend the partition.
    auto grow_map =
        ([this](size_t pool_size, size_t* old_pool_size) FS_TA_NO_THREAD_SAFETY_ANALYSIS {
          return this->GrowMapLocked(pool_size, old_pool_size);
        });

    zx_status_t status;
    // TODO(planders): Allow Extend to take in count.
    if ((status = storage_->Extend(transaction, GetMapDataLocked(), grow_map)) != ZX_OK) {
      return status;
    }

    ZX_DEBUG_ASSERT(GetAvailableLocked() >= count);
  }

  reserved_ += count;
  return ZX_OK;
}

bool Allocator::CheckAllocated(size_t index) const {
  AutoLock lock(&lock_);
  return map_.Get(index, index + 1);
}

size_t Allocator::Allocate(AllocatorReservationKey, PendingWork* transaction) {
  AutoLock lock(&lock_);
  ZX_DEBUG_ASSERT(reserved_ > 0);
  size_t bitoff_start = FindLocked();

  ZX_ASSERT(map_.SetOne(bitoff_start) == ZX_OK);
  storage_->PersistRange(transaction, GetMapDataLocked(), bitoff_start, 1);
  reserved_ -= 1;
  storage_->PersistAllocate(transaction, 1);
  first_free_ = bitoff_start + 1;
  return bitoff_start;
}

void Allocator::Unreserve(AllocatorReservationKey, size_t count) {
  AutoLock lock(&lock_);
#ifdef __Fuchsia__
  ZX_DEBUG_ASSERT(swap_in_.num_bits() == 0);
  ZX_DEBUG_ASSERT(swap_out_.num_bits() == 0);
#endif
  ZX_DEBUG_ASSERT(reserved_ >= count);
  reserved_ -= count;
}

}  // namespace minfs
