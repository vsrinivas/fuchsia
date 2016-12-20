// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/operation.h"

#include "lib/ftl/logging.h"

namespace modular {

void OperationCollection::Hold(Operation* const o) {
  operations_.emplace_back(o);
  o->Run();
}

void OperationCollection::Drop(Operation* const o) {
  auto it = std::remove_if(
      operations_.begin(), operations_.end(),
      [o](const std::unique_ptr<Operation>& p) { return p.get() == o; });
  FTL_DCHECK(it != operations_.end());
  operations_.erase(it, operations_.end());
}

void OperationQueue::Hold(Operation* const o) {
  operations_.emplace(o);
  // Run this operation if it is the only operation in the queue.
  if (operations_.size() == 1) {
    o->Run();
  }
}

void OperationQueue::Drop(Operation* const o) {
  FTL_DCHECK(operations_.front().get() == o);
  operations_.pop();
  if (!operations_.empty()) {
    operations_.front()->Run();
  }
}

Operation::Operation(OperationContainer* const c)
    : container_(c) {}

void Operation::Ready() {
  container_->Hold(this);
}

void Operation::Done() {
  container_->Drop(this);
}

}  // namespace modular
