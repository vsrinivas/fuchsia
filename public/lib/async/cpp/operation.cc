// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/operation.h>

#include <utility>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fxl/logging.h>
#include <trace/event.h>

namespace modular {

constexpr char kModularTraceCategory[] = "modular";
constexpr char kTraceIdKey[] = "id";
constexpr char kTraceInfoKey[] = "info";

OperationContainer::OperationContainer() = default;

OperationContainer::~OperationContainer() = default;

void OperationContainer::Add(OperationBase* const o) {
  FXL_DCHECK(o != nullptr);
  o->SetOwner(this);
  Hold(o);  // Takes ownership.
}

void OperationContainer::Schedule(OperationBase* const o) { o->Schedule(); }

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

fxl::WeakPtr<OperationContainer> OperationCollection::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void OperationCollection::Hold(OperationBase* const o) {
  operations_.emplace_back(o);
  Schedule(o);
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
    Schedule(o);
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
    Schedule(o);
  } else {
    idle_ = true;
  }
}

OperationBase::OperationBase(const char* const trace_name,
                             std::string trace_info)
    // While we transition all operations to be explicitly added to containers
    // with OperationContainer::Add(), some |c|'s are going to be null.
    : weak_ptr_factory_(this),
      trace_name_(trace_name),
      trace_id_(TRACE_NONCE()),
      trace_info_(std::move(trace_info)) {}

OperationBase::~OperationBase() = default;

fxl::WeakPtr<OperationBase> OperationBase::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void OperationBase::SetOwner(OperationContainer* c) {
  FXL_DCHECK(!container_);
  container_ = c->GetWeakPtr();
}

void OperationBase::Schedule() {
  TraceAsyncBegin();

  async::PostTask(async_get_default(),
                  [this, weak = weak_ptr_factory_.GetWeakPtr()] {
                    if (weak) {
                      Run();
                    }
                  });
}

void OperationBase::InvalidateWeakPtrs() {
  weak_ptr_factory_.InvalidateWeakPtrs();
}

void OperationBase::TraceAsyncBegin() {
  TRACE_ASYNC_BEGIN(kModularTraceCategory, trace_name_, trace_id_, kTraceIdKey,
                    trace_id_, kTraceInfoKey, trace_info_);
}

void OperationBase::TraceAsyncEnd() {
  TRACE_ASYNC_END(kModularTraceCategory, trace_name_, trace_id_, kTraceIdKey,
                  trace_id_, kTraceInfoKey, trace_info_);
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
