// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/context_reader_impl.h"

#include <lib/context/cpp/formatting.h>
#include <lib/fidl/cpp/clone.h>

#include "peridot/bin/context_engine/context_repository.h"

namespace modular {

ContextReaderImpl::ContextReaderImpl(
    fuchsia::modular::ComponentScope client_info, ContextRepository* repository,
    fidl::InterfaceRequest<fuchsia::modular::ContextReader> request)
    : binding_(this, std::move(request)), repository_(repository) {
  debug_.client_info = std::move(client_info);
}

ContextReaderImpl::~ContextReaderImpl() = default;

void ContextReaderImpl::Subscribe(
    fuchsia::modular::ContextQuery query,
    fidl::InterfaceHandle<fuchsia::modular::ContextListener> listener) {
  auto listener_ptr = listener.Bind();
  fuchsia::modular::SubscriptionDebugInfo debug_info;
  fidl::Clone(debug_, &debug_info);
  repository_->AddSubscription(std::move(query), std::move(listener_ptr),
                               std::move(debug_info));
}

void ContextReaderImpl::Get(fuchsia::modular::ContextQuery query,
                            GetCallback callback) {
  callback(repository_->Query(query));
}

}  // namespace modular
