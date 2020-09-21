// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the structure used to allocate
// from an on-disk bitmap.

#ifndef SRC_STORAGE_MINFS_ALLOCATOR_ALLOCATOR_H_
#define SRC_STORAGE_MINFS_ALLOCATOR_ALLOCATOR_H_

#include <memory>
#include <mutex>

#include <bitmap/raw-bitmap.h>
#include <bitmap/rle-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/function.h>
#include <fbl/macros.h>
#include <fs/transaction/buffered_operations_builder.h>
#include <minfs/allocator_reservation.h>
#include <minfs/format.h>
#include <minfs/superblock.h>
#include <minfs/writeback.h>

#ifdef __Fuchsia__
#include <fuchsia/minfs/c/fidl.h>
#endif

#include "storage.h"

namespace minfs {

#ifdef __Fuchsia__
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
struct BlockRegion {
  uint64_t offset;
  uint64_t length;
};
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

class Allocator;

// An empty key class which represents the |AllocatorReservation|'s access to
// restricted |Allocator| interfaces.
class AllocatorReservationKey {
 public:
  // Not copyable or movable
  AllocatorReservationKey(const AllocatorReservationKey&) = delete;
  AllocatorReservationKey& operator=(const AllocatorReservationKey&) = delete;
  AllocatorReservationKey(AllocatorReservationKey&&) = delete;
  AllocatorReservationKey& operator=(AllocatorReservationKey&&) = delete;

 private:
  friend AllocatorReservation;
  AllocatorReservationKey() {}
};

// PendingChange tracks pending allocations and will prevent elements from being allocated twice.
// After a change has been committed (passed to a transaction), deallocated elements can still be
// reserved until the transaction actually writes the transaction to the journal. This is because we
// want to prevent data writes going to those blocks until after that.
//
// There can be multiple PendingChange objects per transaction, but, at time of writing, there is
// only one PendingChange for allocations and one PendingChange for deallocations for each allocator
// we support (blocks and inodes), so that's 4 per transaction in total.
//
// This class is not thread-safe and should only be accessed by Allocator, under its lock.
class PendingChange {
 public:
  enum class Kind { kAllocation, kDeallocation };

  ~PendingChange();

  // Not copyable or movable.
  PendingChange(const PendingChange&) = delete;
  PendingChange& operator=(const PendingChange&) = delete;

  Kind kind() const { return kind_; }

  // The change is committed when the change has been made to the persistent bitmap.
  bool is_committed() const { return committed_; }
  void set_committed(bool v) { committed_ = v; }

  // Returns the number of items that need to be reserved for this change. Reserved is where the
  // bitmap indicates the items are free, but they can't be used for some reason.
  size_t GetReservedCount() const;

  // Returns the next unreserved item starting from |start|.
  size_t GetNextUnreserved(size_t start) const;

  // Returns the number of items this change covers.
  size_t item_count() const { return bitmap_.num_bits(); }

  // Access to the underlying bitmap.
  bitmap::RleBitmap& bitmap() { return bitmap_; }

 protected:
  PendingChange(Allocator* allocator, Kind kind);

 private:
  Allocator& allocator_;
  const Kind kind_;
  // The bitmap keeps track of the changes, one bit per element. If kind_ == Kind::kAllocation, each
  // bit is an element to be allocated. If kind_ == Kind::kDeallocation, each bit is an element to
  // be deallocated.
  bitmap::RleBitmap bitmap_;
  // Whether this change is committed to the persistent bitmap.
  bool committed_ = false;
};

class PendingAllocations : public PendingChange {
 public:
  PendingAllocations(Allocator* allocator) : PendingChange(allocator, Kind::kAllocation) {}
};

class PendingDeallocations : public PendingChange {
 public:
  PendingDeallocations(Allocator* allocator) : PendingChange(allocator, Kind::kDeallocation) {}
};

// The Allocator class is used to abstract away the mechanism by which minfs
// allocates objects internally.
//
// This class is thread-safe. However, it is worth pointing out a peculiarity regarding queued
// operations: This class enqueues operations to a caller-supplied BufferedOperationsBuilder as they
// are necessary, but the source of these enqueued buffers may change later. If a caller delays
// writeback, it is their responsibility to ensure no concurrent mutable methods of Allocator are
// accessed while issuing the requests, as these methods may put the buffer-to-be-written in an
// inconsistent state.
class Allocator {
 public:
  virtual ~Allocator();

  Allocator(const Allocator&) = delete;
  Allocator& operator=(const Allocator&) = delete;

  static zx_status_t Create(fs::BufferedOperationsBuilder* builder,
                            std::unique_ptr<AllocatorStorage> storage,
                            std::unique_ptr<Allocator>* out);

  // Return the number of total available elements, after taking reservations into account.
  size_t GetAvailable() const FS_TA_EXCLUDES(lock_);

  // Free an item from the allocator.
  void Free(AllocatorReservation* reservation, size_t index) FS_TA_EXCLUDES(lock_);

#ifdef __Fuchsia__
  // Extract a vector of all currently allocated regions in the filesystem.
  fbl::Vector<BlockRegion> GetAllocatedRegions() const FS_TA_EXCLUDES(lock_);
#endif

  // Returns |true| if |index| is allocated. Returns |false| otherwise.
  bool CheckAllocated(size_t index) const FS_TA_EXCLUDES(lock_);

  // AllocatorReservation Methods:
  //
  // The following methods are restricted to AllocatorReservation via the passkey
  // idiom. They are public, but require an empty |AllocatorReservationKey|.

  // Allocate a single element and return its newly allocated index.
  size_t Allocate(AllocatorReservationKey, AllocatorReservation* reservation) FS_TA_EXCLUDES(lock_);

  // Reserve |count| elements. This is required in order to later allocate them.
  // Outputs a |reservation| which contains reservation details.
  zx_status_t Reserve(AllocatorReservationKey, PendingWork* transaction, size_t count)
      FS_TA_EXCLUDES(lock_);

  // Unreserve |count| elements. This may be called in the event of failure, or if we
  // over-reserved initially.
  //
  // PRECONDITION: AllocatorReservation must have |reserved| > 0.
  void Unreserve(AllocatorReservationKey, size_t count) FS_TA_EXCLUDES(lock_);

  // Allocate / de-allocate elements from the given reservation. This persists the results of any
  // pending allocations/deallocations.
  //
  // Since elements are only ever swapped synchronously, all elements represented in the
  // allocations_ and deallocations_ bitmaps are guaranteed to belong to only one Vnode. This method
  // should only be called in the same thread as the block swaps -- i.e. we should never be
  // resolving blocks for more than one vnode at a time.
  void Commit(PendingWork* transaction, AllocatorReservation* reservation) FS_TA_EXCLUDES(lock_);

 private:
  friend class PendingChange;  // For AddPendingChange & RemovePendingChange.

  Allocator(std::unique_ptr<AllocatorStorage> storage)
      : reserved_(0), first_free_(0), storage_(std::move(storage)) {}

  zx_status_t LoadStorage(fs::BufferedOperationsBuilder* builder) FS_TA_EXCLUDES(lock_);

  // See |GetAvailable()|.
  size_t GetAvailableLocked() const FS_TA_REQUIRES(lock_);

  // Grows the map to |new_size|, returning the current size as |old_size|.
  zx_status_t GrowMapLocked(size_t new_size, size_t* old_size) FS_TA_REQUIRES(lock_);

  // Acquire direct access to the underlying map storage.
  WriteData GetMapDataLocked() FS_TA_REQUIRES(lock_);

  // Find and return a free element. This should only be called when reserved_ > 0, ensuring that at
  // least one free element must exist. This currently assumes that first_free_ is accurately set.
  size_t FindLocked() const FS_TA_REQUIRES(lock_);

  // Find the next unreserved element starting from |start|. Like FindLocked, this should only be
  // called when reserved_ > 0.
  size_t FindNextUnreserved(size_t start) const FS_TA_REQUIRES(lock_);

  // Adds & removes |change| from the vector of pending changes.
  void AddPendingChange(PendingChange* change);
  void RemovePendingChange(PendingChange* change);

  // Protects the allocator's metadata.
  // Does NOT guard the allocator |storage_|.
  mutable std::mutex lock_;

  // Total number of elements reserved by AllocatorReservation objects. Represents the maximum
  // number of elements that are allowed to be allocated or swapped in at a given time. Once an
  // element is marked for allocation or swap, the reserved_ count is updated accordingly. Remaining
  // reserved blocks will be committed by the end of each Vnode operation, with the exception of
  // copy-on-write data blocks. These will be committed asynchronously via the WorkQueue thread.
  // This means that at the time of reservation if |reserved_| > 0, all reserved blocks must
  // belong to vnodes which are already enqueued in the WorkQueue thread.
  size_t reserved_ FS_TA_GUARDED(lock_);

  // Index of the first free element in the map.
  size_t first_free_ FS_TA_GUARDED(lock_);

  // Represents the Allocator's backing storage.
  std::unique_ptr<AllocatorStorage> storage_;
  // A bitmap interface into |storage_|.
  RawBitmap map_ FS_TA_GUARDED(lock_);

  std::vector<PendingChange*> pending_changes_ FS_TA_GUARDED(lock_);
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_ALLOCATOR_ALLOCATOR_H_
