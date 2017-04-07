// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_subscriber_impl.h"

#include "apps/maxwell/src/context_engine/context_repository.h"

namespace maxwell {

ContextSubscriberImpl::ContextSubscriberImpl(ContextRepository* repository)
    : repository_(repository) {}
ContextSubscriberImpl::~ContextSubscriberImpl() = default;

void ContextSubscriberImpl::Subscribe(
    ContextQueryPtr query, fidl::InterfaceHandle<ContextListener> listener) {
  ContextListenerPtr listener_ptr =
      ContextListenerPtr::Create(std::move(listener));
  repository_->AddSubscription(std::move(query), std::move(listener_ptr));
}

}  // namespace maxwell
