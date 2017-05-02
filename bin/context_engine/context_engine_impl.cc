// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_engine_impl.h"

#include "application/lib/app/application_context.h"
#include "apps/maxwell/src/context_engine/context_provider_impl.h"
#include "apps/maxwell/src/context_engine/context_publisher_impl.h"
#include "apps/maxwell/src/context_engine/context_repository.h"
#include "apps/maxwell/src/context_engine/coprocessors/aggregate.h"

namespace maxwell {

ContextEngineImpl::ContextEngineImpl() {
  repository_.AddCoprocessor(new AggregateCoprocessor("focal_entities"));
}

ContextEngineImpl::~ContextEngineImpl() = default;

void ContextEngineImpl::GetPublisher(
    ComponentScopePtr scope,
    fidl::InterfaceRequest<ContextPublisher> request) {
  publisher_bindings_.AddBinding(
      std::make_unique<ContextPublisherImpl>(std::move(scope), &repository_),
      std::move(request));
}

void ContextEngineImpl::GetProvider(
    ComponentScopePtr scope,
    fidl::InterfaceRequest<ContextProvider> request) {
  provider_bindings_.AddBinding(std::make_unique<ContextProviderImpl>(
                                    std::move(scope), &repository_, &debug_),
                                std::move(request));
}

}  // namespace maxwell
