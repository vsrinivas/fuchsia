// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/transaction/buffered_operations_builder.h"

namespace fs {

BufferedOperationsBuilder& BufferedOperationsBuilder::Add(const storage::Operation& new_operation,
                                                          storage::BlockBuffer* buffer) {
  // TODO(rvargas): consider unifying the logic with UnbuffeerdOperationsBuilder.
  for (auto& old_operation : operations_) {
    storage::Operation& operation = old_operation.op;
    if (operation.type != new_operation.type ||
#ifdef __Fuchsia__
        old_operation.vmoid != buffer->vmoid()) {
#else
        old_operation.data != buffer->Data(0)) {
#endif
      continue;
    }

    if (operation.vmo_offset == new_operation.vmo_offset &&
        operation.dev_offset == new_operation.dev_offset) {
      // Take the longer of the operations (if operating on the same blocks).
      if (operation.length <= new_operation.length) {
        operation.length = new_operation.length;
      }
      return *this;
    }
    if ((operation.vmo_offset + operation.length == new_operation.vmo_offset) &&
        (operation.dev_offset + operation.length == new_operation.dev_offset)) {
      // Combine with the previous request, if immediately following.
      operation.length += new_operation.length;
      return *this;
    }
  }

  storage::BufferedOperation buffered_operation;
  buffered_operation.op = new_operation;
#ifdef __Fuchsia__
  buffered_operation.vmoid = buffer->vmoid();
#else
  buffered_operation.data = buffer->Data(0);
#endif

  operations_.push_back(buffered_operation);
  return *this;
}

std::vector<storage::BufferedOperation> BufferedOperationsBuilder::TakeOperations() {
  return std::move(operations_);
}

}  // namespace fs
