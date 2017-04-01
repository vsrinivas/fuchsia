// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_engine_impl.h"

#include "application/lib/app/application_context.h"
#include "apps/maxwell/src/context_engine/context_publisher_impl.h"
#include "apps/maxwell/src/context_engine/context_subscriber_impl.h"
#include "apps/maxwell/src/context_engine/repo.h"

namespace maxwell {

ContextEngineImpl::ContextEngineImpl() {}
ContextEngineImpl::~ContextEngineImpl() = default;

void ContextEngineImpl::RegisterPublisher(
    const fidl::String& url,
    fidl::InterfaceRequest<ContextPublisher> request) {
  publisher_bindings_.AddBinding(
      std::make_unique<ContextPublisherImpl>(new ComponentNode(url), &repo_),
      std::move(request));
}

void ContextEngineImpl::RegisterSubscriber(
    const fidl::String& url,
    fidl::InterfaceRequest<ContextSubscriber> request) {
  subscriber_bindings_.AddBinding(
      std::make_unique<ContextSubscriberImpl>(&repo_), std::move(request));
}

}  // namespace maxwell
