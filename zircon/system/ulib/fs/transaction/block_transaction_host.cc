// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <fs/transaction/block_transaction.h>
#include <fs/transaction/buffered_operations_builder.h>

namespace fs {

zx_status_t TransactionHandler::RunRequests(
    const std::vector<storage::BufferedOperation>& operations) {
  for (const storage::BufferedOperation& operation : operations) {
    internal::BorrowedBuffer buffer(operation.data);
    zx_status_t status = RunOperation(operation.op, &buffer);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

}  // namespace fs
