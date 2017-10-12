// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/module_resolver/module_resolver_impl.h"

namespace maxwell {

ModuleResolverImpl::ModuleResolverImpl() = default;
ModuleResolverImpl::~ModuleResolverImpl() = default;

void ModuleResolverImpl::Connect(fidl::InterfaceRequest<modular::ModuleResolver> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ModuleResolverImpl::FindModules(modular::DaisyPtr daisy,
                   modular::ResolverScoringInfoPtr scoring_info,
                   const FindModulesCallback& done) {
  auto result = modular::FindModulesResult::New();
  result->modules = fidl::Array<modular::ModuleResolverResultPtr>::New(0);
  done(std::move(result));
}

}  // namespace maxwell
