// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/module_resolver/module_resolver_impl.h"

#include "peridot/public/lib/entity/cpp/json.h"

namespace maxwell {

namespace {

void CopyNounsToModuleResolverResult(const modular::DaisyPtr& daisy,
                                     modular::ModuleResolverResultPtr* result) {
  (*result)->initial_nouns.mark_non_null();
  for (auto entry : daisy->nouns) {
    const auto& name = entry.GetKey();
    const auto& noun = entry.GetValue();

    if (noun->is_entity_reference()) {
      // TODO(thatguy): EntityReference, the struct, will go away and be
      // replaced by a string. We simply use the |internal_value| attribute of
      // the struct in its place for now.
      (*result)->initial_nouns[name] = modular::EntityReferenceToJson(
          noun->get_entity_reference()->internal_value);
    } else if (noun->is_json()) {
      (*result)->initial_nouns[name] = noun->get_json();
    }
    // There's nothing to copy over from 'entity_types', since it only
    // specifies noun constraint information, and no actual content.
  }
}

modular::FindModulesResultPtr CreateDefaultResult(
    const modular::DaisyPtr& daisy) {
  auto result = modular::FindModulesResult::New();

  result->modules.resize(0);

  auto print_it = modular::ModuleResolverResult::New();
  print_it->module_id = "resolution_failed";
  print_it->local_name = "n/a";

  CopyNounsToModuleResolverResult(daisy, &print_it);

  result->modules.push_back(std::move(print_it));
  return result;
}

}  // namespace

ModuleResolverImpl::ModuleResolverImpl(std::string manifest_repository_path)
    : next_entry_id_(0) {
  manifest_repository_.reset(new modular::ModuleManifestRepository(
      std::move(manifest_repository_path),
      [this](const modular::ModuleManifestRepository::Entry& entry) {
        OnNewManifestEntry(entry);
      }));
}
ModuleResolverImpl::~ModuleResolverImpl() = default;

void ModuleResolverImpl::Connect(
    fidl::InterfaceRequest<modular::ModuleResolver> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ModuleResolverImpl::FindModules(
    modular::DaisyPtr daisy,
    modular::ResolverScoringInfoPtr scoring_info,
    const FindModulesCallback& done) {
  if (!daisy->verb) {
    // TODO(thatguy): Add no-verb resolution.
    done(CreateDefaultResult(daisy));
    return;
  }

  auto it = verb_to_entry_.find(daisy->verb);
  if (it == verb_to_entry_.end()) {
    done(CreateDefaultResult(daisy));
    return;
  }

  auto results = modular::FindModulesResult::New();
  for (auto id : it->second) {
    auto entry_it = entries_.find(id);
    FXL_CHECK(entry_it != entries_.end()) << id;
    const auto& entry = entry_it->second;

    auto result = modular::ModuleResolverResult::New();
    result->module_id = entry.binary;
    result->local_name = entry.local_name;
    CopyNounsToModuleResolverResult(daisy, &result);

    results->modules.push_back(std::move(result));
  }

  done(std::move(results));
}

void ModuleResolverImpl::OnNewManifestEntry(
    modular::ModuleManifestRepository::Entry new_entry) {
  // Add this new entry info to our local index.
  auto ret = entries_.emplace(next_entry_id_++, std::move(new_entry));

  const auto& id = ret.first->first;
  const auto& entry = ret.first->second;

  verb_to_entry_[entry.verb].insert(id);
}

}  // namespace maxwell
