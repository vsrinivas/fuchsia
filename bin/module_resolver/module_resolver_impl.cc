// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/module_resolver/module_resolver_impl.h"

namespace maxwell {

ModuleResolverImpl::ModuleResolverImpl() = default;
ModuleResolverImpl::~ModuleResolverImpl() = default;

void ModuleResolverImpl::Connect(
    fidl::InterfaceRequest<modular::ModuleResolver> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ModuleResolverImpl::FindModules(
    modular::DaisyPtr daisy,
    modular::ResolverScoringInfoPtr scoring_info,
    const FindModulesCallback& done) {
  auto result = modular::FindModulesResult::New();
  result->modules.resize(0);

  auto print_it = modular::ModuleResolverResult::New();
  print_it->module_id = "file:///system/bin/print_it_module";
  print_it->local_name = "doesn't matter";
  print_it->initial_nouns.mark_non_null();

  for (auto entry : daisy->nouns) {
    const auto& name = entry.GetKey();
    const auto& noun = entry.GetValue();

    // Ignore 'text' and 'entity_type' nouns for now.
    // TODO(thatguy): Don't ignore these types.
    if (noun->is_entity_reference()) {
      print_it->initial_nouns[name] = noun->get_entity_reference().Clone();
    }
  }

  result->modules.push_back(std::move(print_it));

  done(std::move(result));
}

}  // namespace maxwell
