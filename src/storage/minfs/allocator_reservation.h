// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the structure used to allocate
// from an on-disk bitmap.

#ifndef SRC_STORAGE_MINFS_ALLOCATOR_RESERVATION_H_
#define SRC_STORAGE_MINFS_ALLOCATOR_RESERVATION_H_

#include <fbl/function.h>
#include <fbl/macros.h>

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/pending_work.h"
#include "src/storage/minfs/superblock.h"

#ifdef __Fuchsia__
#include <fuchsia/minfs/c/fidl.h>
#endif

namespace minfs {

// Forward declaration for a reference to the internal allocator.
class Allocator;
class PendingAllocations;
class PendingDeallocations;

// This class represents a reservation from an Allocator to save a particular number of reserved
// elements for later allocation. Allocation for reserved elements must be done through the
// AllocatorReservation class.
// This class is thread-compatible.
// This class is not assignable, copyable, or moveable.
class AllocatorReservation {
 public:
  AllocatorReservation(Allocator* allocator);

  // Not copyable or movable.
  AllocatorReservation(const AllocatorReservation&) = delete;
  AllocatorReservation& operator=(const AllocatorReservation&) = delete;

  ~AllocatorReservation();

  // Returns |ZX_OK| when |allocator| reserves |reserved| elements and |this| is successfully
  // initialized. Returns an error if not enough elements are available for reservation,
  // or there was previous reservation.
  zx_status_t Reserve(PendingWork* transaction, size_t reserved);

  // Allocate a new item in allocator_. Return the index of the newly allocated item.
  size_t Allocate();

  // Deallocate a new item from allocate_.
  void Deallocate(size_t element);

  // Unreserve all currently reserved items.
  void Cancel();

#ifdef __Fuchsia__
  // Swap the element currently allocated at |old_index| for a new index.
  // If |old_index| is 0, a new block will still be allocated, but no blocks will be de-allocated.
  // The swap will not be persisted until a call to Commit is made.
  size_t Swap(size_t old_index);

  //  size_t GetReserved() const { return reserved_; }
#endif

  // Returns the pending allocations. Only Allocator should manipulate it.
  PendingAllocations& GetPendingAllocations(Allocator* allocator);

  // Returns the pending deallocations. Only Allocator should manipulate it.
  PendingDeallocations& GetPendingDeallocations(Allocator* allocator);

  std::unique_ptr<PendingDeallocations> TakePendingDeallocations() {
    return std::move(deallocations_);
  }

  // Commit all pending changes, which means all bitmaps are updated (via the transaction). You most
  // likely don't need to call this because it is called in Minfs::CommitTransaction.
  void Commit(PendingWork* transaction);

  size_t GetReserved() const { return reserved_; }

 private:
  Allocator& allocator_;
  size_t reserved_ = 0;
  std::unique_ptr<PendingAllocations> allocations_;
  std::unique_ptr<PendingDeallocations> deallocations_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_ALLOCATOR_RESERVATION_H_
