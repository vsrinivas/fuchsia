// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/fidl/operation.h"

#include <trace/event.h>
#include <utility>

#include "lib/fxl/logging.h"

namespace modular {

OperationContainer::OperationContainer() = default;

OperationContainer::~OperationContainer() = default;

void OperationContainer::InvalidateWeakPtrs(OperationBase* const o) {
  o->InvalidateWeakPtrs();
}

const char* OperationContainer::GetTraceName(OperationBase* const o) {
  return o->trace_name_;
}

uint64_t OperationContainer::GetTraceId(OperationBase* const o) {
  return o->trace_id_;
}

const std::string& OperationContainer::GetTraceInfo(OperationBase* const o) {
  return o->trace_info_;
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

fxl::WeakPtr<OperationContainer> OperationCollection::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void OperationCollection::Hold(OperationBase* const o) {
  operations_.emplace_back(o);
  TRACE_ASYNC_BEGIN(kModularTraceCategory, GetTraceName(o), GetTraceId(o),
                    kTraceIdKey, GetTraceId(o), kTraceInfoKey, GetTraceInfo(o));
  o->Run();
}

void OperationCollection::Drop(OperationBase* const o) {
  auto it = std::remove_if(
      operations_.begin(), operations_.end(),
      [o](const std::unique_ptr<OperationBase>& p) { return p.get() == o; });
  FXL_DCHECK(it != operations_.end());
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

fxl::WeakPtr<OperationContainer> OperationQueue::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void OperationQueue::Hold(OperationBase* const o) {
  operations_.emplace(o);
  if (idle_) {
    FXL_DCHECK(operations_.size() == 1);
    idle_ = false;
    TRACE_ASYNC_BEGIN(kModularTraceCategory, GetTraceName(o), GetTraceId(o),
                      kTraceIdKey, GetTraceId(o), kTraceInfoKey,
                      GetTraceInfo(o));
    o->Run();
  }
}

void OperationQueue::Drop(OperationBase* const o) {
  FXL_DCHECK(!operations_.empty());
  FXL_DCHECK(operations_.front().get() == o);
  operations_.pop();
}

void OperationQueue::Cont() {
  if (!operations_.empty()) {
    auto o = operations_.front().get();
    TRACE_ASYNC_BEGIN(kModularTraceCategory, GetTraceName(o), GetTraceId(o),
                      kTraceIdKey, GetTraceId(o), kTraceInfoKey,
                      GetTraceInfo(o));
    o->Run();
  } else {
    idle_ = true;
  }
}

OperationBase::OperationBase(const char* trace_name,
                             OperationContainer* const c,
                             std::string trace_info)
    : container_(c->GetWeakPtr()),
      weak_ptr_factory_(this),
      trace_name_(trace_name),
      trace_id_(TRACE_NONCE()),
      trace_info_(std::move(trace_info)) {}

OperationBase::~OperationBase() = default;

void OperationBase::Ready() {
  FXL_DCHECK(container_);
  container_->Hold(this);
}

void OperationBase::InvalidateWeakPtrs() {
  weak_ptr_factory_.InvalidateWeakPtrs();
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
