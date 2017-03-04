// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/operation.h"

#include "lib/ftl/logging.h"

namespace modular {

OperationContainer::OperationContainer() = default;

OperationContainer::~OperationContainer() = default;

OperationCollection::OperationCollection() = default;

OperationCollection::~OperationCollection() = default;

void OperationCollection::Hold(OperationBase* const o) {
  operations_.emplace_back(o);
  o->Run();
}

ftl::WeakPtr<OperationContainer> OperationCollection::Drop(OperationBase* const o) {
  auto it = std::remove_if(
      operations_.begin(), operations_.end(),
      [o](const std::unique_ptr<OperationBase>& p) { return p.get() == o; });
  FTL_DCHECK(it != operations_.end());
  operations_.erase(it, operations_.end());
  return ftl::WeakPtr<OperationContainer>();
}

void OperationCollection::Cont() {
  // Never happens, because Drop() always returns nullptr.
}

OperationQueue::OperationQueue() : weak_ptr_factory_(this) {}

OperationQueue::~OperationQueue() = default;

void OperationQueue::Hold(OperationBase* const o) {
  operations_.emplace(o);
  // Run this operation if it is the only operation in the queue. If
  // there are weak ptrs outstanding, this means there will be a
  // Cont() call that runs the next operation.
  if (operations_.size() == 1 && !weak_ptr_factory_.HasWeakPtrs()) {
    o->Run();
  }
}

ftl::WeakPtr<OperationContainer> OperationQueue::Drop(OperationBase* const o) {
  FTL_DCHECK(operations_.front().get() == o);
  operations_.pop();
  return weak_ptr_factory_.GetWeakPtr();
}

void OperationQueue::Cont() {
  if (!operations_.empty()) {
    operations_.front()->Run();
  }
}

OperationBase::OperationBase(OperationContainer* const c) : container_(c) {}

OperationBase::~OperationBase() = default;

void OperationBase::Ready() {
  container_->Hold(this);
}

ftl::WeakPtr<OperationContainer> OperationBase::DoneStart() {
  return container_->Drop(this);
}

void OperationBase::DoneFinish(ftl::WeakPtr<OperationContainer> container) {
  if (container) {
    container->Cont();
  }
}

}  // namespace modular
