// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/transaction/buffered_operations_builder.h"

namespace fs {

BufferedOperationsBuilder::BufferedOperationsBuilder(TransactionHandler* device)
    : device_(device) {}

void BufferedOperationsBuilder::Add(const storage::Operation& operation,
                                    storage::BlockBuffer* buffer) {
  device_->RunOperation(operation, buffer);
}

std::vector<storage::BufferedOperation> BufferedOperationsBuilder::TakeOperations() {
  return std::move(operations_);
}

}  // namespace fs
