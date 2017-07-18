// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_provider_impl.h"

#include "apps/maxwell/src/context_engine/context_repository.h"

namespace maxwell {

ContextProviderImpl::ContextProviderImpl(ComponentScopePtr scope,
                                         ContextRepository* repository,
                                         ContextDebugImpl* debug)
    : scope_(std::move(scope)), repository_(repository), debug_(debug) {}
ContextProviderImpl::~ContextProviderImpl() {
  // Connection error handlers are not executed when closing from our side, so
  // we need to clean up subscriptions ourselves.
  for (auto& listener : listeners_) {
    repository_->RemoveSubscription(listener.repo_subscription_id);
    debug_->OnRemoveSubscription(listener.debug_subscription_id);
  }
};

void ContextProviderImpl::Subscribe(
    ContextQueryPtr query,
    fidl::InterfaceHandle<ContextListener> listener) {
  ContextListenerPtr listener_ptr =
      ContextListenerPtr::Create(std::move(listener));
  auto it = listeners_.emplace(listeners_.begin());
  it->listener = std::move(listener_ptr);
  it->debug_subscription_id = debug_->OnAddSubscription(*scope_, *query);
  it->repo_subscription_id =
      repository_->AddSubscription(std::move(query), it->listener.get());

  it->listener.set_connection_error_handler([=] {
    repository_->RemoveSubscription(it->repo_subscription_id);
    debug_->OnRemoveSubscription(it->debug_subscription_id);
    listeners_.erase(it);
  });
}

}  // namespace maxwell
