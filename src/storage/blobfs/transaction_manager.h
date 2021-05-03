// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TRANSACTION_MANAGER_H_
#define SRC_STORAGE_BLOBFS_TRANSACTION_MANAGER_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include "src/lib/storage/vfs/cpp/journal/journal.h"
#include "src/lib/storage/vfs/cpp/transaction/device_transaction_handler.h"
#include "src/lib/storage/vfs/cpp/vnode.h"
#include "src/storage/blobfs/allocator/allocator.h"
#include "src/storage/blobfs/metrics.h"

namespace blobfs {

class WritebackWork;

// EnqueueType describes the classes of data which may be enqueued to the underlying storage medium.
enum class EnqueueType {
  kJournal,
  kData,
};

// An interface which controls access to the underlying storage.
class TransactionManager : public fs::DeviceTransactionHandler, public SpaceManager {
 public:
  virtual ~TransactionManager() = default;

  virtual std::shared_ptr<BlobfsMetrics>& GetMetrics() = 0;

  // May return null if the journal isn't set up.
  virtual fs::Journal* GetJournal() = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TRANSACTION_MANAGER_H_
