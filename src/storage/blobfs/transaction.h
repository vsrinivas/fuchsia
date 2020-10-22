// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TRANSACTION_H_
#define SRC_STORAGE_BLOBFS_TRANSACTION_H_

#include <vector>

#include <fs/journal/journal.h>
#include <storage/operation/unbuffered_operations_builder.h>

#include "src/storage/blobfs/allocator/extent-reserver.h"

namespace blobfs {

// Not all combinations are supported.  Data operations are not supported with either trim or
// reserved extents (and there is no requirement to do so).
class BlobTransaction {
 public:
  BlobTransaction() = default;
  BlobTransaction(BlobTransaction&) = delete;
  BlobTransaction& operator=(BlobTransaction&) = delete;

  void AddOperation(storage::UnbufferedOperation operation) {
    operations_.Add(std::move(operation));
  }

  void AddTrimOperation(storage::BufferedOperation operation) {
    trim_.push_back(std::move(operation));
  }

  // When freeing extents, we need to reserve them until after the trim operations have completed.
  void AddReservedExtent(ReservedExtent extent) { reserved_extents_.push_back(std::move(extent)); }

  // Commits this transaction to the journal.  |data| is an optional promise that is responsible for
  // writing data associated with metadata that is part of the transaction.  |callback| will be
  // called if and when the transaction commits successfully.  This is not necessarily the point at
  // which the change is guaranteed to be visible in the event of power-failure, but it is the point
  // at which it is safe to, say, use blocks referenced by this transaction for something else.  For
  // example, after the callback, it would be safe to use blocks referenced by any trim operations
  // within the transaction.  In the event of failure, the callback will not get called, but it will
  // get freed, so avoid any manual memory management within the callback (i.e. avoid "delete x";
  // use a captured std::unique_ptr instead).
  void Commit(fs::Journal& journal, fit::promise<void, zx_status_t> data = {},
              fit::callback<void()> callback = {});

 private:
  storage::UnbufferedOperationsBuilder operations_;
  std::vector<storage::BufferedOperation> trim_;
  std::vector<ReservedExtent> reserved_extents_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TRANSACTION_H_
