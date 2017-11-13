// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "peridot/bin/module_resolver/module_resolver_impl.h"

#include "lib/fsl/tasks/message_loop.h"
#include "peridot/public/lib/entity/cpp/json.h"

namespace maxwell {

namespace {

// << operator for ModuleResolverImpl::EntryId.
std::ostream& operator<<(std::ostream& o,
                         const std::pair<std::string, std::string>& id) {
  return o << id.first << ":" << id.second;
}

void CopyNounsToModuleResolverResult(const modular::DaisyPtr& daisy,
                                     modular::ModuleResolverResultPtr* result) {
  (*result)->initial_nouns.mark_non_null();
  for (auto entry : daisy->nouns) {
    const auto& name = entry.GetKey();
    const auto& noun = entry.GetValue();

    if (noun->is_entity_reference()) {
      (*result)->initial_nouns[name] =
          modular::EntityReferenceToJson(noun->get_entity_reference());
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

std::vector<std::string> GetEntityTypesFromNoun(const modular::NounPtr& noun) {
  if (noun->is_entity_type()) {
    return std::vector<std::string>(noun->get_entity_type().begin(),
                                    noun->get_entity_type().end());
  } else if (noun->is_json()) {
    std::vector<std::string> types;
    if (!modular::ExtractEntityTypesFromJson(noun->get_json(), &types)) {
      FXL_LOG(WARNING) << "Mal-formed JSON in noun: " << noun->get_json();
      return {};
    }
    return types;
  }
  // TODO(thatguy): Add support for other methods of getting Entity types.
  return {};
}

}  // namespace

ModuleResolverImpl::ModuleResolverImpl()
    : already_checking_if_sources_are_ready_(false), weak_factory_(this) {}
ModuleResolverImpl::~ModuleResolverImpl() = default;

void ModuleResolverImpl::AddSource(
    std::string name,
    std::unique_ptr<modular::ModuleManifestSource> repo) {
  FXL_CHECK(bindings_.size() == 0);

  repo->Watch(fsl::MessageLoop::GetCurrent()->task_runner(),
              [this, name]() { OnSourceIdle(name); },
              [this, name](std::string id,
                           const modular::ModuleManifestSource::Entry& entry) {
                OnNewManifestEntry(name, std::move(id), entry);
              },
              [this, name](std::string id) {
                OnRemoveManifestEntry(name, std::move(id));
              });

  sources_.emplace(name, std::move(repo));
}

void ModuleResolverImpl::Connect(
    fidl::InterfaceRequest<modular::ModuleResolver> request) {
  if (!AllSourcesAreReady()) {
    PeriodicCheckIfSourcesAreReady();
    pending_bindings_.push_back(std::move(request));
  } else {
    bindings_.AddBinding(this, std::move(request));
  }
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

  auto verb_it = verb_to_entry_.find(daisy->verb);
  if (verb_it == verb_to_entry_.end()) {
    done(CreateDefaultResult(daisy));
    return;
  }

  std::set<EntryId> result_entries(verb_it->second);

  // For each noun in the Daisy, try to find Modules that provide the types in
  // the noun as constraints.
  for (const auto& noun_entry : daisy->nouns) {
    const auto& name = noun_entry.GetKey();
    const auto& noun = noun_entry.GetValue();

    // TODO(thatguy): Once we grab Entity types from an Entity reference, this
    // will have to be an async call. At this point we'll have to break this
    // entire operation up into parts.
    auto types = GetEntityTypesFromNoun(noun);

    // The types list we have is an OR - any Module that can handle any of the
    // types is valid, So, we union all valid resolutions.
    std::set<EntryId> this_noun_entries;
    for (const auto& type : types) {
      auto noun_it = noun_type_to_entry_.find(std::make_pair(type, name));
      if (noun_it == noun_type_to_entry_.end())
        continue;

      this_noun_entries.insert(noun_it->second.begin(), noun_it->second.end());
    }

    // The target Module must match the types in every noun specified in the
    // Daisy, so here we do a set intersection.
    std::set<EntryId> new_result_entries;
    std::set_intersection(
        result_entries.begin(), result_entries.end(), this_noun_entries.begin(),
        this_noun_entries.end(),
        std::inserter(new_result_entries, new_result_entries.begin()));
    result_entries.swap(new_result_entries);
  }

  if (result_entries.empty()) {
    done(CreateDefaultResult(daisy));
    return;
  }

  auto results = modular::FindModulesResult::New();
  for (auto id : result_entries) {
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

void ModuleResolverImpl::OnSourceIdle(const std::string& source_name) {
  auto res = ready_sources_.insert(source_name);
  FXL_CHECK(res.second) << "Got idle notification twice from " << source_name;

  if (AllSourcesAreReady()) {
    // They are all ready. Bind any pending Connect() calls.
    for (auto& request : pending_bindings_) {
      bindings_.AddBinding(this, std::move(request));
    }
    pending_bindings_.clear();
  }
}

void ModuleResolverImpl::OnNewManifestEntry(
    const std::string& source_name,
    std::string id_in,
    modular::ModuleManifestSource::Entry new_entry) {
  // Add this new entry info to our local index.
  auto ret =
      entries_.emplace(EntryId(source_name, id_in), std::move(new_entry));
  if (!ret.second) {
    // Remove this existing entry first, then add it back in.
    OnRemoveManifestEntry(source_name, id_in);
  }

  const auto& id = ret.first->first;
  const auto& entry = ret.first->second;

  verb_to_entry_[entry.verb].insert(id);

  for (const auto& constraint : entry.noun_constraints) {
    for (const auto& type : constraint.types) {
      noun_type_to_entry_[std::make_pair(type, constraint.name)].insert(id);
    }
  }
}

void ModuleResolverImpl::OnRemoveManifestEntry(const std::string& source_name,
                                               std::string id_in) {
  EntryId id{source_name, id_in};
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    FXL_LOG(WARNING) << "Asked to remove non-existent manifest entry: " << id;
    return;
  }

  const auto& entry = it->second;
  verb_to_entry_[entry.verb].erase(id);

  for (const auto& constraint : entry.noun_constraints) {
    for (const auto& type : constraint.types) {
      noun_type_to_entry_[std::make_pair(type, constraint.name)].erase(id);
    }
  }

  entries_.erase(id);
}

void ModuleResolverImpl::PeriodicCheckIfSourcesAreReady() {
  if (!AllSourcesAreReady()) {
    for (const auto& it : sources_) {
      if (ready_sources_.count(it.first) == 0) {
        FXL_LOG(WARNING) << "Still waiting on source: " << it.first;
      }
    }

    if (already_checking_if_sources_are_ready_)
      return;
    already_checking_if_sources_are_ready_ = true;
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [weak_this = weak_factory_.GetWeakPtr()]() {
          if (weak_this) {
            weak_this->already_checking_if_sources_are_ready_ = false;
            weak_this->PeriodicCheckIfSourcesAreReady();
          }
        },
        fxl::TimeDelta::FromSeconds(10));
  }
}

}  // namespace maxwell
