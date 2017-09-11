// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/context_engine/context_engine_impl.h"

#include "lib/app/cpp/application_context.h"
#include "apps/maxwell/src/context_engine/context_reader_impl.h"
#include "apps/maxwell/src/context_engine/context_repository.h"
#include "apps/maxwell/src/context_engine/context_writer_impl.h"

namespace maxwell {

ContextEngineImpl::ContextEngineImpl() = default;
ContextEngineImpl::~ContextEngineImpl() = default;

void ContextEngineImpl::AddBinding(
    fidl::InterfaceRequest<ContextEngine> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ContextEngineImpl::GetWriter(
    ComponentScopePtr client_info,
    fidl::InterfaceRequest<ContextWriter> request) {
  writers_.emplace_back(std::make_unique<ContextWriterImpl>(
      std::move(client_info), &repository_, std::move(request)));
}

void ContextEngineImpl::GetReader(
    ComponentScopePtr client_info,
    fidl::InterfaceRequest<ContextReader> request) {
  readers_.emplace_back(std::make_unique<ContextReaderImpl>(
      std::move(client_info), &repository_, std::move(request)));
}

void ContextEngineImpl::GetContextDebug(
    fidl::InterfaceRequest<ContextDebug> request) {
  repository_.AddDebugBinding(std::move(request));
}

}  // namespace maxwell
