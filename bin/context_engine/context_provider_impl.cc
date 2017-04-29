// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_provider_impl.h"

#include "apps/maxwell/src/context_engine/context_repository.h"

namespace maxwell {

ContextProviderImpl::ContextProviderImpl(ContextRepository* repository)
    : repository_(repository) {}
ContextProviderImpl::~ContextProviderImpl() = default;

void ContextProviderImpl::Subscribe(
    ContextQueryPtr query,
    fidl::InterfaceHandle<ContextListener> listener) {
  ContextListenerPtr listener_ptr =
      ContextListenerPtr::Create(std::move(listener));
  auto it = listeners_.emplace(listeners_.begin(), std::move(listener_ptr));
  auto subscription_id =
      repository_->AddSubscription(std::move(query), it->get());
  it->set_connection_error_handler([this, it, subscription_id] {
    listeners_.erase(it);
    repository_->RemoveSubscription(subscription_id);
  });
}

}  // namespace maxwell
