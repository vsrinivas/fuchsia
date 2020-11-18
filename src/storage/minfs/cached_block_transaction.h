// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_CACHED_BLOCK_TRANSACTION_H_
#define SRC_STORAGE_MINFS_CACHED_BLOCK_TRANSACTION_H_

#include <lib/zx/status.h>

#include "src/storage/minfs/allocator_reservation.h"

namespace minfs {

// CachedBlockTransaction holds block reservations across multiple calls. Unlike Transaction(see
// minfs::Transaction), CachedBlockTransaction does not require a filesystem wide lock to be held
// throughout the duration of the object's life time. CachedBlockTransaction currently works only
// for block reservation.
class CachedBlockTransaction {
 public:
  explicit CachedBlockTransaction(std::unique_ptr<AllocatorReservation> block_reservation)
      : block_reservation_(std::move(block_reservation)) {}

  CachedBlockTransaction() = delete;

  // Not copyable or movable
  CachedBlockTransaction(const CachedBlockTransaction&) = delete;
  CachedBlockTransaction& operator=(const CachedBlockTransaction&) = delete;
  CachedBlockTransaction(CachedBlockTransaction&&) = delete;
  CachedBlockTransaction& operator=(CachedBlockTransaction&&) = delete;

  std::unique_ptr<AllocatorReservation> TakeBlockReservations() {
    return (std::move(block_reservation_));
  }

 private:
  std::unique_ptr<AllocatorReservation> block_reservation_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_CACHED_BLOCK_TRANSACTION_H_
