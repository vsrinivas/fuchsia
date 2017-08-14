// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/debug.h"

namespace maxwell {

namespace {

bool CompareAgentScopes(const AgentScope& a, const AgentScope& b) {
  return a.url < b.url;
}

bool CompareModuleScopes(const ModuleScope& a, const ModuleScope& b) {
  return (a.url == b.url && a.story_id < b.story_id) || a.url < b.url;
}

bool CompareScopes(const ComponentScopePtr& a, const ComponentScopePtr& b) {
  if (a->is_global_scope() || b->is_global_scope()) {
    return a->is_global_scope() && !b->is_global_scope();

  } else if (a->is_agent_scope() || b->is_agent_scope()) {
    if (a->is_agent_scope() && b->is_agent_scope()) {
      return CompareAgentScopes(*a->get_agent_scope(), *b->get_agent_scope());
    } else {
      return a->is_agent_scope();
    }
  } else if (a->is_module_scope() || b->is_module_scope()) {
    if (a->is_module_scope() && b->is_module_scope()) {
      return CompareModuleScopes(*a->get_module_scope(),
                                 *b->get_module_scope());
    } else {
      return a->is_module_scope();
    }
  } else {
    return false;
  }
}

}  // namespace

ContextDebugImpl::ContextDebugImpl() : subscriptions_(CompareScopes) {}

ContextDebugImpl::SubscriptionId ContextDebugImpl::OnAddSubscription(
    const ComponentScope& subscriber,
    const ContextQueryForTopics& query) {
  SubscriptionId id = subscriptions_.emplace(subscriber.Clone(), query.Clone());
  DispatchAll();
  return id;
}

void ContextDebugImpl::OnRemoveSubscription(SubscriptionId subscription) {
  subscriptions_.erase(subscription);
  DispatchAll();
}

void ContextDebugImpl::WatchSubscribers(
    fidl::InterfaceHandle<SubscriberListener> listener) {
  auto listener_ptr = SubscriberListenerPtr::Create(std::move(listener));
  Dispatch(listener_ptr.get());
  listeners_.AddInterfacePtr(std::move(listener_ptr));
}

void ContextDebugImpl::DispatchAll() {
  listeners_.ForAllPtrs(
      std::bind(&ContextDebugImpl::Dispatch, this, std::placeholders::_1));
}

void ContextDebugImpl::Dispatch(SubscriberListener* listener) {
  auto updates = fidl::Array<SubscriberUpdatePtr>::New(0);
  const ComponentScopePtr* scope = nullptr;
  fidl::Array<ContextQueryForTopicsPtr>* queries;
  for (const auto& subscription : subscriptions_) {
    // Create a scope/update for each distinct subscriber in the multimap.
    if (!scope || CompareScopes(*scope, subscription.first)) {
      // Be explicit about StructPtr (vs. SubscriberUpdatePtr) to effectively
      // static-assert that we don't want this inlined.
      fidl::StructPtr<SubscriberUpdate> update = SubscriberUpdate::New();
      scope = &subscription.first;
      update->subscriber = (*scope)->Clone();
      queries = &update->queries;
      updates.push_back(std::move(update));
    }
    queries->push_back(subscription.second->Clone());
  }
  listener->OnUpdate(std::move(updates));
}

}  // namespace maxwell
