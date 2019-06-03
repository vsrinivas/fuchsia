// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/intelligence_services_impl.h"

#include <fuchsia/modular/cpp/fidl.h>

namespace modular {

IntelligenceServicesImpl::IntelligenceServicesImpl(
    fuchsia::modular::ComponentScope scope,
    fuchsia::modular::ContextEngine* context_engine)
    : scope_(std::move(scope)), context_engine_(context_engine) {}

fuchsia::modular::ComponentScope IntelligenceServicesImpl::CloneScope() {
  fuchsia::modular::ComponentScope scope;
  fidl::Clone(scope_, &scope);
  return scope;
}

void IntelligenceServicesImpl::GetContextReader(
    fidl::InterfaceRequest<fuchsia::modular::ContextReader> request) {
  context_engine_->GetReader(CloneScope(), std::move(request));
}

void IntelligenceServicesImpl::GetContextWriter(
    fidl::InterfaceRequest<fuchsia::modular::ContextWriter> request) {
  context_engine_->GetWriter(CloneScope(), std::move(request));
}

}  // namespace modular
