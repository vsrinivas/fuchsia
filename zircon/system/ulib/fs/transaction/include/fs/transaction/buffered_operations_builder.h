// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TRANSACTION_BUFFERED_OPERATIONS_BUILDER_H_
#define FS_TRANSACTION_BUFFERED_OPERATIONS_BUILDER_H_

#include <vector>

#include <fbl/macros.h>
#include <fs/transaction/block_transaction.h>
#include <storage/buffer/block_buffer.h>
#include <storage/operation/operation.h>
#ifdef __Fuchsia__
#include <storage/buffer/owned_vmoid.h>
#endif

namespace fs {

// A builder which helps clients collect and coalesce BufferedOperations which target the same
// in-memory / on-disk structures.
class BufferedOperationsBuilder {
 public:
  // The provided transaction handler must outlive this object.
  // Note that a handler is only required for host code, and will not be used for Fuchsia code.
  BufferedOperationsBuilder(TransactionHandler* device);
  ~BufferedOperationsBuilder() {}

  // Adds a request to the list of operations.
  // The host version of this code performs the requested operation at this point, so nothing is
  // really added to the list of operations.
  // Note that there is some coalescing of requests performed here, and mixing different types
  // of operations is not supported at this time.
  void Add(const storage::Operation& operation, storage::BlockBuffer* buffer);

  // Removes the vector of requests, and returns them to the caller.
  std::vector<storage::BufferedOperation> TakeOperations();

#ifdef __Fuchsia__
  // Adds a vmoid that needs to be detached once the operations have completed.
  void AddVmoid(storage::OwnedVmoid vmoid) {
    vmoids_.push_back(std::move(vmoid));
  }
#endif

  DISALLOW_COPY_ASSIGN_AND_MOVE(BufferedOperationsBuilder);

 private:
  std::vector<storage::BufferedOperation> operations_;

#ifdef __Fuchsia__
  std::vector<storage::OwnedVmoid> vmoids_;
#else
  TransactionHandler* device_;
#endif
};

}  // namespace fs

#endif  // FS_TRANSACTION_BUFFERED_OPERATIONS_BUILDER_H_
