// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "replay_tree.h"

#include <fs/transaction/writeback.h>
#include <storage/operation/buffered_operation.h>

namespace fs {
namespace internal {

// static.
zx_status_t RangeTraits::Update(const RangeContainer* other, uint64_t start, uint64_t end,
                                RangeContainer* obj) {
  if (other) {
    // Index by "dev_offset", but only allow merging BufferedOperations if:
    // - Their dev offsets, lengths are contiguous (enforced by range library),
    // - The difference between vmo offsets of an operation equals the difference
    // between dev offsets.
    //
    // This enables merging between contiguous operations like:
    // - vmo_offset = 1
    // - dev_offset = 10
    // - length = 1
    // And
    // - vmo_offset = 2
    // - dev_offset = 11
    // - length = 1
    //
    // But disallows merging between operations such as:
    // - vmo_offset = 1
    // - dev_offset = 10
    // - length = 1
    // And
    // - vmo_offset = 5   <-- Not contiguous with prior operation!
    // - dev_offset = 11
    // - length = 1
    if ((other->operation.op.vmo_offset - obj->operation.op.vmo_offset) !=
        (other->operation.op.dev_offset - obj->operation.op.dev_offset)) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  obj->Update(start, end);
  return ZX_OK;
}

}  // namespace internal

ReplayTree::ReplayTree() = default;

void ReplayTree::insert(storage::BufferedOperation operation) {
  internal::BufferedOperationRange range((internal::RangeContainer(operation)));

  // Erase all prior operations which touch the same dev_offset.
  tree_.erase(range);

  // Utilize the newest operations touching dev_offset.
  tree_.insert(range);
}

}  // namespace fs
