// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/context_engine_impl.h"

#include <lib/app/cpp/startup_context.h>

#include "peridot/bin/context_engine/context_reader_impl.h"
#include "peridot/bin/context_engine/context_repository.h"
#include "peridot/bin/context_engine/context_writer_impl.h"

namespace modular {

ContextEngineImpl::ContextEngineImpl(
    fuchsia::modular::EntityResolver* const entity_resolver)
    : entity_resolver_(entity_resolver) {}
ContextEngineImpl::~ContextEngineImpl() = default;

fxl::WeakPtr<ContextDebugImpl> ContextEngineImpl::debug() {
  return repository_.debug()->GetWeakPtr();
}

void ContextEngineImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::modular::ContextEngine> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ContextEngineImpl::GetWriter(
    fuchsia::modular::ComponentScope client_info,
    fidl::InterfaceRequest<fuchsia::modular::ContextWriter> request) {
  writers_.emplace_back(std::make_unique<ContextWriterImpl>(
      std::move(client_info), &repository_, entity_resolver_,
      std::move(request)));
}

void ContextEngineImpl::GetReader(
    fuchsia::modular::ComponentScope client_info,
    fidl::InterfaceRequest<fuchsia::modular::ContextReader> request) {
  readers_.emplace_back(std::make_unique<ContextReaderImpl>(
      std::move(client_info), &repository_, std::move(request)));
}

void ContextEngineImpl::GetContextDebug(
    fidl::InterfaceRequest<fuchsia::modular::ContextDebug> request) {
  repository_.AddDebugBinding(std::move(request));
}

}  // namespace modular
