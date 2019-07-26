// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <blobfs/allocator.h>
#include <blobfs/blob.h>
#include <blobfs/metrics.h>
#include <fbl/unique_ptr.h>
#include <fs/block-txn.h>
#include <fs/vnode.h>

namespace blobfs {

class WritebackWork;

// EnqueueType describes the classes of data which may be enqueued to the
// underlying storage medium.
enum class EnqueueType {
  kJournal,
  kData,
};

// An interface which controls access to the underlying storage.
class TransactionManager : public fs::TransactionHandler, public SpaceManager {
 public:
  virtual ~TransactionManager() = default;
  virtual BlobfsMetrics& Metrics() = 0;

  // Returns the capacity of the writeback buffer in blocks.
  virtual size_t WritebackCapacity() const = 0;

  // Initializes a new unit of WritebackWork associated with a Writebacktarget.
  virtual zx_status_t CreateWork(fbl::unique_ptr<WritebackWork>* out, Blob* vnode) = 0;

  // Enqueues |work| to the appropriate buffer.
  // If the data is journaled, |work| will be transmitted to the journal, where it will be
  // persisted only after consistency is ensured.
  // If the data is not journaled, |work| will be transmitted directly to the writeback buffer /
  // persistent storage.
  virtual zx_status_t EnqueueWork(fbl::unique_ptr<WritebackWork> work, EnqueueType type) = 0;
};

}  // namespace blobfs
