// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/context_engine/context_engine_impl.h"

#include "lib/app/cpp/application_context.h"
#include "peridot/bin/context_engine/context_reader_impl.h"
#include "peridot/bin/context_engine/context_repository.h"
#include "peridot/bin/context_engine/context_writer_impl.h"

namespace maxwell {

ContextEngineImpl::ContextEngineImpl(
    modular::EntityResolver* const entity_resolver)
    : entity_resolver_(entity_resolver) {}
ContextEngineImpl::~ContextEngineImpl() = default;

fxl::WeakPtr<ContextDebugImpl> ContextEngineImpl::debug() {
  return repository_.debug()->GetWeakPtr();
}

void ContextEngineImpl::AddBinding(
    f1dl::InterfaceRequest<ContextEngine> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ContextEngineImpl::GetWriter(
    ComponentScopePtr client_info,
    f1dl::InterfaceRequest<ContextWriter> request) {
  writers_.emplace_back(std::make_unique<ContextWriterImpl>(
      std::move(client_info), &repository_, entity_resolver_,
      std::move(request)));
}

void ContextEngineImpl::GetReader(
    ComponentScopePtr client_info,
    f1dl::InterfaceRequest<ContextReader> request) {
  readers_.emplace_back(std::make_unique<ContextReaderImpl>(
      std::move(client_info), &repository_, std::move(request)));
}

void ContextEngineImpl::GetContextDebug(
    f1dl::InterfaceRequest<ContextDebug> request) {
  repository_.AddDebugBinding(std::move(request));
}

}  // namespace maxwell
