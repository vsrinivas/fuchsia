// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <limits>
#include <memory>
#include <utility>

#include <bitmap/raw-bitmap.h>

#include "src/storage/minfs/allocator/allocator.h"

namespace minfs {

PendingChange::PendingChange(Allocator* allocator, Kind kind)
    : allocator_(*allocator), kind_(kind) {
  allocator_.AddPendingChange(this);
}

PendingChange::~PendingChange() { allocator_.RemovePendingChange(this); }

size_t PendingChange::GetReservedCount() const {
  // Allocations are reserved before we've committed, but after we've committed, we don't need to
  // reserve any more.
  //
  // Deallocations don't need to be reserved before we've committed, but after we've committed, we
  // can't use these blocks for data until the metadata has gone through via the transaction; we
  // have to do this because writes to data blocks aren't sequenced against anything else.
  return committed_ == (kind_ == Kind::kAllocation) ? 0 : bitmap_.num_bits();
}

size_t PendingChange::GetNextUnreserved(size_t start) const {
  // See comment above regarding reserved/unreserved.
  if (committed_ == (kind_ == Kind::kAllocation)) {
    return start;
  }
  size_t item = std::numeric_limits<size_t>::max();
  bitmap_.Find(false, start, item, 1, &item);
  return item;
}

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
  std::scoped_lock lock(lock_);
  return GetAvailableLocked();
}

// Loops through all pending changes and finds the next unreserved block that we might be able to
// allocate.
size_t Allocator::FindNextUnreserved(size_t start) const {
  auto iter = pending_changes_.begin();
  for (size_t unchanged = 0; unchanged < pending_changes_.size(); ++unchanged) {
    size_t next_start = (*iter)->GetNextUnreserved(start);
    // If next_state == start, it means that the change doesn't overlap with start.
    if (next_start != start) {
      // We expect there to always be a free block.
      ZX_ASSERT(next_start < map_.size());
      unchanged = 0;
      start = next_start;
    }
    if (++iter == pending_changes_.end()) {
      iter = pending_changes_.begin();
    }
  }
  return start;
}

size_t Allocator::FindLocked() const {
  ZX_DEBUG_ASSERT(reserved_ > 0);
  size_t start = first_free_;
  size_t index = map_.size();

  for (;;) {
    // Start by looking for an unreserved block.
    start = FindNextUnreserved(start);

    if (index != map_.size() && start == index) {
      ZX_DEBUG_ASSERT(!map_.GetOne(index));
      return index;
    }

    // Now search for the next free element in the map.
    ZX_ASSERT(map_.Find(false, start, map_.size(), 1, &index) == ZX_OK);
    start = index;
  }
}

void Allocator::Commit(PendingWork* transaction, AllocatorReservation* reservation) {
  PendingAllocations& allocations = reservation->GetPendingAllocations(this);
  PendingDeallocations& deallocations = reservation->GetPendingDeallocations(this);

  std::scoped_lock lock(lock_);

  ZX_ASSERT(!allocations.is_committed() && !deallocations.is_committed());

  if (allocations.item_count() == 0 && deallocations.item_count() == 0) {
    return;
  }

  for (const auto& range : allocations.bitmap()) {
    // Ensure that none of the bits are already allocated.
    ZX_DEBUG_ASSERT(map_.Scan(range.bitoff, range.end(), false));

    // Swap in the new bits.
    zx_status_t status = map_.Set(range.bitoff, range.end());
    ZX_DEBUG_ASSERT(status == ZX_OK);
    storage_->PersistRange(transaction, GetMapDataLocked(), range.bitoff, range.bitlen);
  }

  for (const auto& range : deallocations.bitmap()) {
    // Ensure that all bits are already allocated.
    ZX_DEBUG_ASSERT(map_.Get(range.bitoff, range.end()));

    // Swap out the old bits.
    zx_status_t status = map_.Clear(range.bitoff, range.end());
    ZX_DEBUG_ASSERT(status == ZX_OK);
    storage_->PersistRange(transaction, GetMapDataLocked(), range.bitoff, range.bitlen);
  }

  // Update count of allocated blocks.
  if (allocations.item_count() > deallocations.item_count()) {
    storage_->PersistAllocate(transaction, allocations.item_count() - deallocations.item_count());
  } else if (deallocations.item_count() > allocations.item_count()) {
    storage_->PersistRelease(transaction, deallocations.item_count() - allocations.item_count());
  }

  // Mark the changes as committed.
  allocations.set_committed(true);
  deallocations.set_committed(true);
}

void Allocator::Free(AllocatorReservation* reservation, size_t index) {
  PendingAllocations& allocations = reservation->GetPendingAllocations(this);
  PendingDeallocations& deallocations = reservation->GetPendingDeallocations(this);
  std::scoped_lock lock(lock_);
  if (allocations.bitmap().GetOne(index)) {
    allocations.bitmap().ClearOne(index);
  } else {
    ZX_DEBUG_ASSERT(map_.GetOne(index));
    ZX_ASSERT(deallocations.bitmap().SetOne(index) == ZX_OK);
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

zx_status_t Allocator::Reserve(AllocatorReservationKey, PendingWork* transaction, size_t count) {
  std::scoped_lock lock(lock_);
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
  std::scoped_lock lock(lock_);
  return map_.Get(index, index + 1);
}

size_t Allocator::Allocate(AllocatorReservationKey, AllocatorReservation* reservation) {
  PendingAllocations& allocations = reservation->GetPendingAllocations(this);

  std::scoped_lock lock(lock_);
  ZX_DEBUG_ASSERT(reserved_ > 0);

  size_t new_index = FindLocked();
  ZX_DEBUG_ASSERT(!allocations.bitmap().GetOne(new_index));
  ZX_ASSERT(allocations.bitmap().SetOne(new_index) == ZX_OK);
  reserved_--;
  first_free_ = new_index + 1;
  return new_index;
}

void Allocator::Unreserve(AllocatorReservationKey, size_t count) {
  std::scoped_lock lock(lock_);
  ZX_DEBUG_ASSERT(reserved_ >= count);
  reserved_ -= count;
}

void Allocator::AddPendingChange(PendingChange* change) {
  std::scoped_lock lock(lock_);
  pending_changes_.push_back(change);
}

void Allocator::RemovePendingChange(PendingChange* change) {
  std::scoped_lock lock(lock_);
  if (change->GetReservedCount() > 0) {
    auto range = change->bitmap().begin();
    if (range != change->bitmap().end() && range->start() < first_free_) {
      first_free_ = range->start();
    }
  }
  pending_changes_.erase(std::remove(pending_changes_.begin(), pending_changes_.end(), change),
                         pending_changes_.end());
}

}  // namespace minfs
