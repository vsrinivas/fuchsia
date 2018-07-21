// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/module_resolver_fake.h"

namespace modular {

ModuleResolverFake::ModuleResolverFake() {
  find_modules_response_.results.resize(0);
}

ModuleResolverFake::~ModuleResolverFake() = default;

void ModuleResolverFake::FindModules(fuchsia::modular::FindModulesQuery query,
                                     FindModulesCallback callback) {
  if (find_modules_validate_fn_) {
    find_modules_validate_fn_(query);
  }
  callback(std::move(find_modules_response_));
}

void ModuleResolverFake::GetModuleManifest(fidl::StringPtr module_id,
                                           GetModuleManifestCallback callback) {
  get_module_manifest_validate_fn_(module_id);
  callback(std::move(manifest_));
}

void ModuleResolverFake::FindModulesByTypes(
    fuchsia::modular::FindModulesByTypesQuery query,
    FindModulesByTypesCallback callback) {}

void ModuleResolverFake::Connect(
    fidl::InterfaceRequest<fuchsia::modular::ModuleResolver> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ModuleResolverFake::SetManifest(
    fuchsia::modular::ModuleManifestPtr manifest) {
  manifest_ = std::move(manifest);
}

void ModuleResolverFake::AddFindModulesResult(
    fuchsia::modular::FindModulesResult result) {
  find_modules_response_.results.push_back(std::move(result));
}

void ModuleResolverFake::SetFindModulesValidation(
    std::function<void(const fuchsia::modular::FindModulesQuery&)> fn) {
  find_modules_validate_fn_ = std::move(fn);
}

void ModuleResolverFake::SetGetModuleManifestValidation(
    std::function<void(const fidl::StringPtr&)> fn) {
  get_module_manifest_validate_fn_ = std::move(fn);
}

}  // namespace modular
