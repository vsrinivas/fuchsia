// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINFS_PENDING_WORK_H_
#define MINFS_PENDING_WORK_H_

#include <zircon/device/block.h>
#include <zircon/types.h>

#include <minfs/format.h>
#include <storage/buffer/block_buffer.h>
#include <storage/operation/operation.h>

namespace minfs {
// Types of data to use with read and write transactions.
#ifdef __Fuchsia__
using WriteData = zx_handle_t;
#else
using WriteData = const void*;
#endif

// Represents an interface which can be used to store pending work to be written to disk at a later
// time.
class PendingWork {
 public:
  virtual ~PendingWork() = default;

#ifdef __Fuchsia__
  // Identifies that an extent of metadata blocks should be written to disk at a later point in
  // time.
  // TODO(rvargas): Remove this version.
  virtual void EnqueueMetadata(WriteData source, storage::Operation operation) = 0;
#else
  // Identifies that an extent of metadata blocks should be written to disk at a later point in
  // time.
  virtual void EnqueueMetadata(storage::Operation operation, storage::BlockBuffer* buffer) = 0;
#endif

  // Identifies that an extent of data blocks should be written to disk at a later point in time.
  // Write to data blocks must be done in a separate transaction from metadata updates to ensure
  // that all user data goes out to disk before associated metadata.
  virtual void EnqueueData(WriteData source, storage::Operation operation) = 0;
};
}  // namespace minfs

#endif  // MINFS_PENDING_WORK_H_
