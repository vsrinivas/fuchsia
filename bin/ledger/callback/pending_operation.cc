// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/pending_operation.h"

#include <algorithm>

namespace callback {

ftl::Closure PendingOperationManager::ManagePendingOperation(
    std::unique_ptr<PendingOperation> operation) {
  PendingOperation* ptr = operation.get();
  pending_operations_.push_back(std::move(operation));

  return [this, ptr]() {
    auto it =
        std::find_if(pending_operations_.begin(), pending_operations_.end(),
                     [ptr](const std::unique_ptr<PendingOperation>& c) {
                       return c.get() == ptr;
                     });
    FTL_DCHECK(it != pending_operations_.end());
    pending_operations_.erase(it);
  };
}

}  // namespace callback
