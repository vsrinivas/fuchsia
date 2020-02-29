// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <fs/transaction/block_transaction.h>

namespace fs {

zx_status_t TransactionHandler::RunRequests(
    const std::vector<storage::BufferedOperation>& operations) {
  // The actual operations are performed while building the requests.
  ZX_DEBUG_ASSERT(operations.empty());
  return ZX_OK;
}

}  // namespace fs
