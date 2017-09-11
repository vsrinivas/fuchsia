// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/pending_operation.h"

#include <algorithm>

namespace callback {

PendingOperationManager::PendingOperationManager() : weak_ptr_factory_(this) {}

PendingOperationManager::~PendingOperationManager() {}

fxl::Closure PendingOperationManager::ManagePendingOperation(
    std::unique_ptr<PendingOperation> operation) {
  PendingOperation* ptr = operation.get();
  pending_operations_.push_back(std::move(operation));

  // We use a weak pointer to PendingOperationManager to allow the manager to be
  // deleted.
  return [ weak_this = weak_ptr_factory_.GetWeakPtr(), ptr ]() {
    if (!weak_this) {
      return;
    }

    auto it = std::find_if(weak_this->pending_operations_.begin(),
                           weak_this->pending_operations_.end(),
                           [ptr](const std::unique_ptr<PendingOperation>& c) {
                             return c.get() == ptr;
                           });
    FXL_DCHECK(it != weak_this->pending_operations_.end());
    weak_this->pending_operations_.erase(it);
  };
}

}  // namespace callback
