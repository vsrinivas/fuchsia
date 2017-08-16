// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_reader_impl.h"

#include "apps/maxwell/lib/context/formatting.h"
#include "apps/maxwell/services/context/debug.fidl.h"
#include "apps/maxwell/src/context_engine/context_repository.h"

namespace maxwell {

ContextReaderImpl::ContextReaderImpl(
    ComponentScopePtr client_info,
    ContextRepository* repository,
    fidl::InterfaceRequest<ContextReader> request)
    : binding_(this, std::move(request)), repository_(repository) {
  debug_ = SubscriptionDebugInfo::New();
  debug_->client_info = std::move(client_info);
}

ContextReaderImpl::~ContextReaderImpl() = default;

void ContextReaderImpl::Subscribe(
    ContextQueryPtr query,
    fidl::InterfaceHandle<ContextListener> listener) {
  auto listener_ptr = ContextListenerPtr::Create(std::move(listener));
  repository_->AddSubscription(std::move(query), std::move(listener_ptr),
                               debug_.Clone());
}

}  // namespace maxwell
