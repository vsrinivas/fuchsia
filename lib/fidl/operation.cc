// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/fidl/operation.h"

#include "lib/ftl/logging.h"

namespace modular {

OperationContainer::OperationContainer() = default;

OperationContainer::~OperationContainer() = default;

void OperationContainer::InvalidateWeakPtrs(OperationBase* const o) {
  o->InvalidateWeakPtrs();
}

OperationCollection::OperationCollection() : weak_ptr_factory_(this) {}

OperationCollection::~OperationCollection() {
  // We invalidate weakptrs to all Operation<>s first before destroying them, so
  // that an outstanding FlowToken<> that gets destroyed in the process doesn't
  // erroneously call Operation<>::Done.
  for (auto& operation : operations_) {
    InvalidateWeakPtrs(operation.get());
  }
}

bool OperationCollection::Empty() {
  return operations_.empty();
}

ftl::WeakPtr<OperationContainer> OperationCollection::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void OperationCollection::Hold(OperationBase* const o) {
  operations_.emplace_back(o);
  o->Run();
}

void OperationCollection::Drop(
    OperationBase* const o) {
  auto it = std::remove_if(
      operations_.begin(), operations_.end(),
      [o](const std::unique_ptr<OperationBase>& p) { return p.get() == o; });
  FTL_DCHECK(it != operations_.end());
  operations_.erase(it, operations_.end());
}

void OperationCollection::Cont() {
  // no-op for operation collection.
}

OperationQueue::OperationQueue() : weak_ptr_factory_(this) {}

OperationQueue::~OperationQueue() {
  // We invalidate weakptrs to all Operation<>s first before destroying them, so
  // that an outstanding FlowToken<> that gets destroyed in the process doesn't
  // erroneously call Operation<>::Done.
  while (!operations_.empty()) {
    InvalidateWeakPtrs(operations_.front().get());
    operations_.pop();
  }
}

bool OperationQueue::Empty() {
  return operations_.empty();
}

ftl::WeakPtr<OperationContainer> OperationQueue::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void OperationQueue::Hold(OperationBase* const o) {
  operations_.emplace(o);
  if (idle_) {
    FTL_DCHECK(operations_.size() == 1);
    idle_ = false;
    o->Run();
  }
}

void OperationQueue::Drop(OperationBase* const o) {
  FTL_DCHECK(!operations_.empty());
  FTL_DCHECK(operations_.front().get() == o);
  operations_.pop();
}

void OperationQueue::Cont() {
  if (!operations_.empty()) {
    operations_.front()->Run();
  } else {
    idle_ = true;
  }
}

OperationBase::OperationBase(OperationContainer* const c)
    : container_(c->GetWeakPtr()), weak_ptr_factory_(this) {}

OperationBase::~OperationBase() = default;

void OperationBase::Ready() {
  FTL_DCHECK(container_);
  container_->Hold(this);
}

OperationBase::FlowTokenBase::FlowTokenBase(OperationBase* const op)
    : refcount_(new int), weak_op_(op->weak_ptr_factory_.GetWeakPtr()) {
  *refcount_ = 1;
}

OperationBase::FlowTokenBase::FlowTokenBase(const FlowTokenBase& other)
    : refcount_(other.refcount_), weak_op_(other.weak_op_) {
  ++*refcount_;
}

OperationBase::FlowTokenBase::~FlowTokenBase() {
  --*refcount_;
  if (*refcount_ == 0) {
    delete refcount_;
  }
}

}  // namespace modular
