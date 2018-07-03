// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/debug.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>

#include "peridot/bin/context_engine/context_repository.h"

namespace modular {

ContextDebugImpl::ContextDebugImpl(const ContextRepository* const repository)
    : repository_(repository), weak_ptr_factory_(this) {}
ContextDebugImpl::~ContextDebugImpl() = default;

fxl::WeakPtr<ContextDebugImpl> ContextDebugImpl::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void ContextDebugImpl::OnValueChanged(
    const std::set<Id>& parent_ids, const Id& id,
    const fuchsia::modular::ContextValue& value) {
  fuchsia::modular::ContextDebugValue update;
  update.parent_ids.resize(0);
  for (const auto& it : parent_ids) {
    update.parent_ids.push_back(it);
  }
  update.id = id;
  fuchsia::modular::ContextValue value_clone;
  fidl::Clone(value, &value_clone);
  update.value = fidl::MakeOptional(std::move(value_clone));
  DispatchOneValue(std::move(update));
}

void ContextDebugImpl::OnValueRemoved(const Id& id) {
  fuchsia::modular::ContextDebugValue update;
  update.id = id;
  update.parent_ids.resize(0);
  DispatchOneValue(std::move(update));
}

void ContextDebugImpl::OnSubscriptionAdded(
    const Id& id, const fuchsia::modular::ContextQuery& query,
    const fuchsia::modular::SubscriptionDebugInfo& debug_info) {
  fuchsia::modular::ContextDebugSubscription update;
  update.id = id;
  fuchsia::modular::ContextQuery query_clone;
  fidl::Clone(query, &query_clone);
  update.query = fidl::MakeOptional(std::move(query_clone));
  fuchsia::modular::SubscriptionDebugInfo debug_info_clone;
  fidl::Clone(debug_info, &debug_info_clone);
  update.debug_info = fidl::MakeOptional(std::move(debug_info_clone));
  DispatchOneSubscription(std::move(update));
}

void ContextDebugImpl::OnSubscriptionRemoved(const Id& id) {
  fuchsia::modular::ContextDebugSubscription update;
  update.id = id;
  DispatchOneSubscription(std::move(update));
}

util::IdleWaiter* ContextDebugImpl::GetIdleWaiter() { return &idle_waiter_; }

void ContextDebugImpl::Watch(
    fidl::InterfaceHandle<fuchsia::modular::ContextDebugListener> listener) {
  FXL_LOG(INFO) << "Watch(): entered";
  auto listener_ptr = listener.Bind();
  // Build a complete state snapshot and send it to |listener|.
  auto all_values =
      fidl::VectorPtr<fuchsia::modular::ContextDebugValue>::New(0);
  for (const auto& entry : repository_->values_) {
    fuchsia::modular::ContextDebugValue update;
    update.id = entry.first;
    fuchsia::modular::ContextValue value_clone;
    fidl::Clone(entry.second.value, &value_clone);
    update.value = fidl::MakeOptional(std::move(value_clone));
    update.parent_ids.resize(0);
    for (const auto& it : repository_->graph_.GetParents(entry.first)) {
      update.parent_ids.push_back(it);
    }
    all_values.push_back(std::move(update));
  }
  listener_ptr->OnValuesChanged(std::move(all_values));
  // TODO(thatguy): Add subscriptions.

  listeners_.AddInterfacePtr(std::move(listener_ptr));
}

void ContextDebugImpl::WaitUntilIdle(WaitUntilIdleCallback callback) {
  idle_waiter_.WaitUntilIdle(callback);
}

void ContextDebugImpl::DispatchOneValue(
    fuchsia::modular::ContextDebugValue value) {
  fidl::VectorPtr<fuchsia::modular::ContextDebugValue> values;
  values.push_back(std::move(value));
  DispatchValues(std::move(values));
}

void ContextDebugImpl::DispatchValues(
    fidl::VectorPtr<fuchsia::modular::ContextDebugValue> values) {
  for (const auto& listener : listeners_.ptrs()) {
    fidl::VectorPtr<fuchsia::modular::ContextDebugValue> values_clone;
    fidl::Clone(values, &values_clone);
    (*listener)->OnValuesChanged(std::move(values_clone));
  }
}

void ContextDebugImpl::DispatchOneSubscription(
    fuchsia::modular::ContextDebugSubscription value) {
  fidl::VectorPtr<fuchsia::modular::ContextDebugSubscription> values;
  values.push_back(std::move(value));
  DispatchSubscriptions(std::move(values));
}

void ContextDebugImpl::DispatchSubscriptions(
    fidl::VectorPtr<fuchsia::modular::ContextDebugSubscription> subscriptions) {
  for (const auto& listener : listeners_.ptrs()) {
    fidl::VectorPtr<fuchsia::modular::ContextDebugSubscription>
        subscriptions_clone;
    fidl::Clone(subscriptions, &subscriptions_clone);
    (*listener)->OnSubscriptionsChanged(std::move(subscriptions_clone));
  }
}

}  // namespace modular
