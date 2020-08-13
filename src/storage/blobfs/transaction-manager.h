// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TRANSACTION_MANAGER_H_
#define SRC_STORAGE_BLOBFS_TRANSACTION_MANAGER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fs/journal/journal.h>
#include <fs/transaction/legacy_transaction_handler.h>
#include <fs/vnode.h>

#include "allocator/allocator.h"
#include "metrics.h"

namespace blobfs {

class WritebackWork;

// EnqueueType describes the classes of data which may be enqueued to the
// underlying storage medium.
enum class EnqueueType {
  kJournal,
  kData,
};

// An interface which controls access to the underlying storage.
class TransactionManager : public fs::LegacyTransactionHandler, public SpaceManager {
 public:
  virtual ~TransactionManager() = default;
  virtual BlobfsMetrics* Metrics() = 0;

  // Returns the capacity of the writeback buffer in blocks.
  virtual size_t WritebackCapacity() const = 0;

  virtual fs::Journal* journal() = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TRANSACTION_MANAGER_H_
