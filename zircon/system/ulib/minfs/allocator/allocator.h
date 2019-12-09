// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file describes the structure used to allocate
// from an on-disk bitmap.

#ifndef ZIRCON_SYSTEM_ULIB_MINFS_ALLOCATOR_ALLOCATOR_H_
#define ZIRCON_SYSTEM_ULIB_MINFS_ALLOCATOR_ALLOCATOR_H_

#include <memory>

#include <bitmap/raw-bitmap.h>
#include <bitmap/rle-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/function.h>
#include <fbl/macros.h>
#include <fs/transaction/block_transaction.h>
#include <minfs/allocator-reservation.h>
#include <minfs/format.h>
#include <minfs/mutex.h>
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

// An empty key class which represents the |AllocatorReservation|'s access to
// restricted |Allocator| interfaces.
class AllocatorReservationKey {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AllocatorReservationKey);

 private:
  friend AllocatorReservation;
  AllocatorReservationKey() {}
};

// The Allocator class is used to abstract away the mechanism by which minfs
// allocates objects internally.
//
// This class is thread-safe. However, it is worth pointing out a peculiarity
// regarding |WriteTxn|: This class enqueues operations to a caller-supplied
// WriteTxn as they are necessary, but the source of these enqueued buffers may
// change immediately after |Enqueue()| completes. If a caller delays writeback,
// it is their responsibility to ensure no concurrent mutable methods of
// Allocator are accessed while Transacting the |WriteTxn|, as these methods
// may put the buffer-to-be-written in an inconsistent state.
class Allocator {
 public:
  virtual ~Allocator();

  Allocator(const Allocator&) = delete;
  Allocator& operator=(const Allocator&) = delete;

  static zx_status_t Create(fs::ReadTxn* txn, std::unique_ptr<AllocatorStorage> storage,
                            std::unique_ptr<Allocator>* out);

  // Return the number of total available elements, after taking reservations into account.
  size_t GetAvailable() const FS_TA_EXCLUDES(lock_);

  // Free an item from the allocator.
  void Free(PendingWork* transaction, size_t index) FS_TA_EXCLUDES(lock_);

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
  size_t Allocate(AllocatorReservationKey, PendingWork* transaction) FS_TA_EXCLUDES(lock_);

  // Reserve |count| elements. This is required in order to later allocate them.
  // Outputs a |reservation| which contains reservation details.
  zx_status_t Reserve(AllocatorReservationKey, PendingWork* transaction, size_t count,
                      AllocatorReservation* reservation) FS_TA_EXCLUDES(lock_);

  // Unreserve |count| elements. This may be called in the event of failure, or if we
  // over-reserved initially.
  //
  // PRECONDITION: AllocatorReservation must have |reserved| > 0.
  void Unreserve(AllocatorReservationKey, size_t count) FS_TA_EXCLUDES(lock_);

#ifdef __Fuchsia__
  // Mark |index| for de-allocation by adding it to the swap_out map,
  // and return the index of a new element to be swapped in.
  // This is currently only used for the block allocator.
  //
  // PRECONDITION: |index| must be allocated in the internal map.
  // PRECONDITION: AllocatorReservation must have |reserved| > 0.
  size_t Swap(AllocatorReservationKey, size_t index) FS_TA_EXCLUDES(lock_);

  // Allocate / de-allocate elements from the swap_in / swap_out maps (respectively).
  // This persists the results of |Swap|.
  //
  // Since elements are only ever swapped synchronously, all elements represented in the swap_in_
  // and swap_out_ maps are guaranteed to belong to only one Vnode. This method should only be
  // called in the same thread as the block swaps -- i.e. we should never be resolving blocks for
  // more than one vnode at a time.
  void SwapCommit(AllocatorReservationKey, PendingWork* transaction) FS_TA_EXCLUDES(lock_);
#endif

 private:
  Allocator(std::unique_ptr<AllocatorStorage> storage)
      : reserved_(0), first_free_(0), storage_(std::move(storage)) {}

  zx_status_t LoadStorage(fs::ReadTxn* txn) FS_TA_EXCLUDES(lock_);

  // See |GetAvailable()|.
  size_t GetAvailableLocked() const FS_TA_REQUIRES(lock_);

  // Grows the map to |new_size|, returning the current size as |old_size|.
  zx_status_t GrowMapLocked(size_t new_size, size_t* old_size) FS_TA_REQUIRES(lock_);

  // Acquire direct access to the underlying map storage.
  WriteData GetMapDataLocked() const FS_TA_REQUIRES(lock_);

  // Find and return a free element. This should only be called when reserved_ > 0,
  // ensuring that at least one free element must exist.
  size_t FindLocked() const FS_TA_REQUIRES(lock_);

  // Protects the allocator's metadata.
  // Does NOT guard the allocator |storage_|.
  mutable Mutex lock_;

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

#ifdef __Fuchsia__
  // Bitmap of elements to be allocated on SwapCommit.
  bitmap::RleBitmap swap_in_ FS_TA_GUARDED(lock_);
  // Bitmap of elements to be de-allocated on SwapCommit.
  bitmap::RleBitmap swap_out_ FS_TA_GUARDED(lock_);
#endif
};

}  // namespace minfs

#endif  // ZIRCON_SYSTEM_ULIB_MINFS_ALLOCATOR_ALLOCATOR_H_
