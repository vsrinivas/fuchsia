// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/context_reader_impl.h"

#include "lib/fidl/cpp/clone.h"
#include "lib/context/cpp/formatting.h"
#include "peridot/bin/context_engine/context_repository.h"

namespace modular {

ContextReaderImpl::ContextReaderImpl(
    ComponentScope client_info,
    ContextRepository* repository,
    fidl::InterfaceRequest<ContextReader> request)
    : binding_(this, std::move(request)), repository_(repository) {
  debug_.client_info = std::move(client_info);
}

ContextReaderImpl::~ContextReaderImpl() = default;

void ContextReaderImpl::Subscribe(
    ContextQuery query,
    fidl::InterfaceHandle<ContextListener> listener) {
  auto listener_ptr = listener.Bind();
  SubscriptionDebugInfo debug_info;
  fidl::Clone(debug_, &debug_info);
  repository_->AddSubscription(std::move(query), std::move(listener_ptr),
                               std::move(debug_info));
}

void ContextReaderImpl::Get(
    ContextQuery query,
    GetCallback callback) {
  callback(repository_->Query(query));
}

}  // namespace modular
