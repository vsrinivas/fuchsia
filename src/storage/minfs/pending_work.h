// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_PENDING_WORK_H_
#define SRC_STORAGE_MINFS_PENDING_WORK_H_

#include <zircon/device/block.h>
#include <zircon/types.h>

#include <storage/buffer/block_buffer.h>
#include <storage/operation/operation.h>

#include "src/storage/minfs/format.h"

namespace minfs {
// Types of data to use with read and write transactions.
#ifdef __Fuchsia__
using WriteData = zx_handle_t;
#else
using WriteData = void*;
#endif

// Represents an interface which can be used to store pending work to be written to disk at a later
// time.
class PendingWork {
 public:
  virtual ~PendingWork() = default;

  // Enqueues a metadata-write operation.
  virtual void EnqueueMetadata(storage::Operation operation, storage::BlockBuffer* buffer) = 0;

  // Enqueues a data-write operation.
  // Write to data blocks must be done in a separate transaction from metadata updates to ensure
  // that all user data goes out to disk before associated metadata.
  virtual void EnqueueData(storage::Operation operation, storage::BlockBuffer* buffer) = 0;

  // Allocates a block in the data section and returns the block allocated.
  virtual size_t AllocateBlock() = 0;

  // Deallocates a block in the data section and returns the block allocated.
  virtual void DeallocateBlock(size_t block) = 0;
};
}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_PENDING_WORK_H_
