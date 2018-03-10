// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/debug.h"

#include "peridot/bin/context_engine/context_repository.h"

namespace maxwell {

ContextDebugImpl::Activity::Activity(fxl::WeakPtr<ContextDebugImpl> debug)
    : debug_(debug) {
  debug_->activity_ = this;
}

ContextDebugImpl::Activity::~Activity() {
  if (debug_) {
    debug_->activity_ = nullptr;
    debug_->PostIdleCheck();
  }
}

ContextDebugImpl::ContextDebugImpl(const ContextRepository* const repository)
    : repository_(repository),
      message_loop_(fsl::MessageLoop::GetCurrent()),
      weak_ptr_factory_(this) {}
ContextDebugImpl::~ContextDebugImpl() = default;

fxl::WeakPtr<ContextDebugImpl> ContextDebugImpl::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void ContextDebugImpl::OnValueChanged(const std::set<Id>& parent_ids,
                                      const Id& id,
                                      const ContextValuePtr& value) {
  auto update = ContextDebugValue::New();
  update->parent_ids = f1dl::VectorPtr<f1dl::StringPtr>::From(parent_ids);
  update->id = id;
  update->value = value.Clone();
  DispatchOneValue(std::move(update));
}

void ContextDebugImpl::OnValueRemoved(const Id& id) {
  auto update = ContextDebugValue::New();
  update->id = id;
  update->parent_ids = f1dl::VectorPtr<f1dl::StringPtr>::New(0);
  DispatchOneValue(std::move(update));
}

void ContextDebugImpl::OnSubscriptionAdded(
    const Id& id,
    const ContextQueryPtr& query,
    const SubscriptionDebugInfoPtr& debug_info) {
  auto update = ContextDebugSubscription::New();
  update->id = id;
  update->query = query.Clone();
  update->debug_info = debug_info.Clone();
  DispatchOneSubscription(std::move(update));
}

void ContextDebugImpl::OnSubscriptionRemoved(const Id& id) {
  auto update = ContextDebugSubscription::New();
  update->id = id;
  DispatchOneSubscription(std::move(update));
}

ContextDebugImpl::ActivityToken ContextDebugImpl::RegisterOngoingActivity() {
  FXL_DCHECK(message_loop_ == fsl::MessageLoop::GetCurrent());

  if (activity_) {
    return ActivityToken(activity_);
  } else {
    // |activity_| is set in the |Activity| constructor and cleared in
    // the destructor
    return fxl::MakeRefCounted<Activity>(weak_ptr_factory_.GetWeakPtr());
  }
}

void ContextDebugImpl::Watch(
    f1dl::InterfaceHandle<ContextDebugListener> listener) {
  FXL_LOG(INFO) << "Watch(): entered";
  auto listener_ptr = listener.Bind();
  // Build a complete state snapshot and send it to |listener|.
  auto all_values = f1dl::VectorPtr<ContextDebugValuePtr>::New(0);
  for (const auto& entry : repository_->values_) {
    auto update = ContextDebugValue::New();
    update->id = entry.first;
    update->value = entry.second.value.Clone();
    update->parent_ids = f1dl::VectorPtr<f1dl::StringPtr>::From(
        repository_->graph_.GetParents(entry.first));
    all_values.push_back(std::move(update));
  }
  listener_ptr->OnValuesChanged(std::move(all_values));
  // TODO(thatguy): Add subscriptions.

  listeners_.AddInterfacePtr(std::move(listener_ptr));
}

void ContextDebugImpl::WaitUntilIdle(const WaitUntilIdleCallback& callback) {
  idle_waiters_.push_back(callback);
  PostIdleCheck();
}

void ContextDebugImpl::DispatchOneValue(ContextDebugValuePtr value) {
  f1dl::VectorPtr<ContextDebugValuePtr> values;
  values.push_back(value.Clone());
  DispatchValues(std::move(values));
}

void ContextDebugImpl::DispatchValues(
    f1dl::VectorPtr<ContextDebugValuePtr> values) {
  listeners_.ForAllPtrs([&values](ContextDebugListener* listener) {
    listener->OnValuesChanged(values.Clone());
  });
}

void ContextDebugImpl::DispatchOneSubscription(
    ContextDebugSubscriptionPtr value) {
  f1dl::VectorPtr<ContextDebugSubscriptionPtr> values;
  values.push_back(value.Clone());
  DispatchSubscriptions(std::move(values));
}

void ContextDebugImpl::DispatchSubscriptions(
    f1dl::VectorPtr<ContextDebugSubscriptionPtr> subscriptions) {
  listeners_.ForAllPtrs([&subscriptions](ContextDebugListener* listener) {
    listener->OnSubscriptionsChanged(subscriptions.Clone());
  });
}

void ContextDebugImpl::PostIdleCheck() {
  if (!(idle_waiters_.empty() || activity_ || idle_check_pending_)) {
    FXL_DCHECK(message_loop_);
    message_loop_->PostQuitTask();
    idle_check_pending_ = true;
  }
}

bool ContextDebugImpl::FinishIdleCheck() {
  if (idle_check_pending_) {
    message_loop_->RunUntilIdle();
    if (!activity_) {
      for (const auto& callback : idle_waiters_) {
        callback();
      }
      idle_waiters_.clear();
    }
    // Otherwise, |PostIdleCheck| will be invoked again when |activity_| is
    // released.

    idle_check_pending_ = false;
    return true;
  } else {
    return false;
  }
}

}  // namespace maxwell
