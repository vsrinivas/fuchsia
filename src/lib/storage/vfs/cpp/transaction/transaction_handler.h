// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TRANSACTION_TRANSACTION_HANDLER_H_
#define FS_TRANSACTION_TRANSACTION_HANDLER_H_

#include <zircon/assert.h>
#include <zircon/device/block.h>

#include <vector>

#include <fbl/algorithm.h>
#include <fbl/macros.h>
#include <fbl/vector.h>
#include <storage/buffer/block_buffer.h>
#include <storage/operation/operation.h>

namespace fs {

// Access the "blkno"-th block within data.
// "blkno = 0" corresponds to the first block within data.
inline void* GetBlock(uint64_t block_size, const void* data, uint64_t blkno) {
  ZX_ASSERT(block_size <= (blkno + 1) * block_size);  // Avoid overflow
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(data) +
                                 static_cast<uintptr_t>(block_size * blkno));
}

// TransactionHandler defines the interface that must be fulfilled
// for an entity to issue transactions to the underlying device.
class TransactionHandler {
 public:
  virtual ~TransactionHandler() = default;

  // Translates a filesystem-level block number to a block-device-level block number.
  virtual uint64_t BlockNumberToDevice(uint64_t block_num) const = 0;

  // A convenience method for running a single operation. |buffer| provides access to the memory
  // buffer that is referenced by |operation|.  The values inside |operation| are expected to be
  // filesystem-level block numbers.  This method blocks until the operation completes, so it is
  // suitable for host-based reads and writes and for simple Fuchsia-based reads. Regular Fuchsia IO
  // is expected to be issued using the RunRequests method.  A default implementation is provided.
  virtual zx_status_t RunOperation(const storage::Operation& operation,
                                   storage::BlockBuffer* buffer);

  // Runs the provided operations against the backing block device.
  // The values inside |operations| are expected to be filesystem-level block numbers.
  // This method blocks until the operation completes, but the implementation for Fuchsia forwards
  // the requests to the underlying BlockDevice so it is expected that this interface will be
  // upgraded to be fully asynchronous at some point.
  // The caller should use a BufferedOperationsBuilder to construct the request.
  virtual zx_status_t RunRequests(const std::vector<storage::BufferedOperation>& operations) = 0;

  virtual zx_status_t Flush() { return ZX_ERR_NOT_SUPPORTED; }
};

}  // namespace fs

#endif  // FS_TRANSACTION_TRANSACTION_HANDLER_H_
