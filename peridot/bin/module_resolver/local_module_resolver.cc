// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/module_resolver/local_module_resolver.h"

#include <algorithm>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/context/cpp/context_helper.h>
#include <lib/entity/cpp/json.h>
#include <src/lib/fxl/strings/split_string.h>

#include "peridot/lib/fidl/clone.h"

namespace modular {
namespace {

// << operator for LocalModuleResolver::ManifestId.
std::ostream& operator<<(std::ostream& o,
                         const std::pair<std::string, std::string>& id) {
  return o << id.first << ":" << id.second;
}

}  // namespace

LocalModuleResolver::LocalModuleResolver()
    : already_checking_if_sources_are_ready_(false), weak_factory_(this) {}

LocalModuleResolver::~LocalModuleResolver() = default;

std::set<LocalModuleResolver::ManifestId> LocalModuleResolver::FindHandlers(
    ModuleUri handler) {
  // Search through each known repository source for |handler|, and return a
  // set<ManifestId> containing them.
  std::set<ManifestId> manifests;
  for (auto& source : ready_sources_) {
    auto it = manifests_.find(ManifestId(source, handler));
    if (it != manifests_.end()) {
      manifests.insert(it->first);
    }
  }
  return manifests;
}

void LocalModuleResolver::AddSource(
    std::string source_name, std::unique_ptr<ModuleManifestSource> repo) {
  FXL_CHECK(bindings_.size() == 0);

  auto ptr = repo.get();
  sources_.emplace(source_name, std::move(repo));

  ptr->Watch(
      async_get_default_dispatcher(),
      [this, source_name]() { OnSourceIdle(source_name); },
      [this, source_name](std::string module_uri,
                          fuchsia::modular::ModuleManifest manifest) {
        OnNewManifestEntry(source_name, module_uri, std::move(manifest));
      },
      [this, source_name](std::string module_uri) {
        OnRemoveManifestEntry(source_name, std::move(module_uri));
      });
}

void LocalModuleResolver::Connect(
    fidl::InterfaceRequest<fuchsia::modular::ModuleResolver> request) {
  if (!AllSourcesAreReady()) {
    PeriodicCheckIfSourcesAreReady();
    pending_bindings_.push_back(std::move(request));
  } else {
    bindings_.AddBinding(this, std::move(request));
  }
}

class LocalModuleResolver::FindModulesCall
    : public Operation<fuchsia::modular::FindModulesResponse> {
 public:
  FindModulesCall(LocalModuleResolver* local_module_resolver,
                  fuchsia::modular::FindModulesQuery query,
                  ResultCall result_call)
      : Operation("LocalModuleResolver::FindModulesCall",
                  std::move(result_call)),
        local_module_resolver_(local_module_resolver),
        query_(std::move(query)) {}

  // Finds all modules that match |query_|.
  //
  // The specified action is used to filter potential modules, and
  // the associated parameters are required to match in both name and type. If
  // |query_.module_handler| is specified, then the search for the action and
  // parameters are restricted to the specified handler.
  void Run() override {
    FlowToken flow{this, &response_};

    // 1. If a handler is specified, use only that for |candidates_|.
    if (!query_.handler.is_null()) {
      auto found_handlers =
          local_module_resolver_->FindHandlers(query_.handler);
      if (found_handlers.empty()) {
        response_ = CreateEmptyResponseWithStatus(
            fuchsia::modular::FindModulesStatus::UNKNOWN_HANDLER);
        return;
      }

      candidates_ = found_handlers;
    }

    // 2. Find all modules that can handle the action and then take an
    // intersection |candidates_| if its non-empty.
    auto action_it =
        local_module_resolver_->action_to_manifests_.find(query_.action);
    if (action_it != local_module_resolver_->action_to_manifests_.end()) {
      if (!candidates_.empty()) {
        std::set<ManifestId> new_candidates;
        if (action_it != local_module_resolver_->action_to_manifests_.end()) {
          candidates_ = action_it->second;
          std::set_intersection(
              candidates_.begin(), candidates_.end(), action_it->second.begin(),
              action_it->second.end(),
              std::inserter(new_candidates, new_candidates.begin()));
          candidates_ = new_candidates;
        }
      } else {
        candidates_ = action_it->second;
      }
    }

    // 3. For each parameter in the FindModulesQuery, try to filter
    // |candidates_| to only the modules that provide the types in the
    // parameter constraints.
    if (!candidates_.empty()) {
      for (const auto& parameter_entry : query_.parameter_constraints) {
        ProcessParameterTypes(parameter_entry.param_name,
                              parameter_entry.param_types);
      }
    }

    FinalizeResponse(flow);
  }

 private:
  // |parameter_name| and |types| come from the FindModulesQuery.
  void ProcessParameterTypes(const std::string& parameter_name,
                             const std::vector<std::string>& types) {
    std::set<ManifestId> parameter_type_entries;
    for (const auto& type : types) {
      std::set<ManifestId> found_entries =
          GetManifestsMatchingParameterByTypeAndName(type, parameter_name);
      parameter_type_entries.insert(found_entries.begin(), found_entries.end());
    }

    std::set<ManifestId> new_result_entries;
    // All parameters in the query must be handled by the candidates. For each
    // parameter that is processed, filter out any existing results that can't
    // also handle the new parameter type.
    std::set_intersection(
        candidates_.begin(), candidates_.end(), parameter_type_entries.begin(),
        parameter_type_entries.end(),
        std::inserter(new_result_entries, new_result_entries.begin()));

    candidates_.swap(new_result_entries);
  }

  // Returns the ManifestIds of all entries with a parameter that matches the
  // provided name and type.
  std::set<ManifestId> GetManifestsMatchingParameterByTypeAndName(
      const std::string& parameter_type, const std::string& parameter_name) {
    std::set<ManifestId> found_entries;
    auto found_manifests_it =
        local_module_resolver_->parameter_type_and_name_to_manifests_.find(
            std::make_pair(parameter_type, parameter_name));
    if (found_manifests_it !=
        local_module_resolver_->parameter_type_and_name_to_manifests_.end()) {
      found_entries.insert(found_manifests_it->second.begin(),
                           found_manifests_it->second.end());
    }
    return found_entries;
  }

  // At this point |candidates_| contains all the modules that satisfy the
  // query. The purpose of this method is to create a response using these
  // candidates.
  void FinalizeResponse(FlowToken flow) {
    response_ = CreateEmptyResponseWithStatus(
        fuchsia::modular::FindModulesStatus::SUCCESS);
    if (candidates_.empty()) {
      return;
    }

    for (auto manifest_id : candidates_) {
      auto entry_it = local_module_resolver_->manifests_.find(manifest_id);
      FXL_CHECK(entry_it != local_module_resolver_->manifests_.end())
          << manifest_id;

      const auto& manifest = entry_it->second;
      fuchsia::modular::FindModulesResult result;
      result.module_id = manifest.binary;
      result.manifest = fuchsia::modular::ModuleManifest::New();
      result.manifest->intent_filters.resize(0);
      fidl::Clone(manifest, result.manifest.get());

      response_.results.push_back(std::move(result));
    }
  }

  fuchsia::modular::FindModulesResponse CreateEmptyResponseWithStatus(
      fuchsia::modular::FindModulesStatus status) {
    fuchsia::modular::FindModulesResponse response;
    response.status = status;
    response.results.resize(0);
    return response;
  }

  fuchsia::modular::FindModulesResponse response_;
  LocalModuleResolver* const local_module_resolver_;
  fuchsia::modular::FindModulesQuery query_;

  std::set<ManifestId> candidates_;
};

void LocalModuleResolver::FindModules(fuchsia::modular::FindModulesQuery query,
                                      FindModulesCallback callback) {
  operations_.Add(std::make_unique<FindModulesCall>(this, std::move(query),
                                                    std::move(callback)));
}

void LocalModuleResolver::OnSourceIdle(const std::string& source_name) {
  auto res = ready_sources_.insert(source_name);
  if (!res.second) {
    // It's OK for us to get an idle notification twice from a repo. This
    // happens, for instance, if there's a network problem and we have to
    // re-establish it.
    return;
  }

  if (AllSourcesAreReady()) {
    // They are all ready. Bind any pending Connect() calls.
    for (auto& request : pending_bindings_) {
      bindings_.AddBinding(this, std::move(request));
    }
    pending_bindings_.clear();
  }
}

void LocalModuleResolver::OnNewManifestEntry(
    const std::string& source_name, std::string module_uri,
    fuchsia::modular::ModuleManifest new_manifest) {
  FXL_LOG(INFO) << "New Module manifest for binary " << module_uri << " with "
                << new_manifest.intent_filters->size() << " intent filters.";
  auto manifest_id = ManifestId(source_name, module_uri);
  // Add this new manifest info to our local index.
  if (manifests_.count(manifest_id) > 0) {
    // Remove this existing manifest first, then add it back in.
    OnRemoveManifestEntry(source_name, module_uri);
  }
  if (new_manifest.intent_filters->size() == 0) {
    new_manifest.intent_filters.resize(0);
  }
  auto ret = manifests_.emplace(manifest_id, std::move(new_manifest));
  FXL_CHECK(ret.second);
  const auto& manifest = ret.first->second;
  for (const auto& intent_filter : *manifest.intent_filters) {
    action_to_manifests_[intent_filter.action].insert(manifest_id);

    for (const auto& constraint : intent_filter.parameter_constraints) {
      parameter_type_and_name_to_manifests_[std::make_pair(constraint.type,
                                                           constraint.name)]
          .insert(manifest_id);
      parameter_type_to_manifests_[constraint.type].insert(manifest_id);
    }
  }
}

void LocalModuleResolver::OnRemoveManifestEntry(const std::string& source_name,
                                                std::string module_uri) {
  ManifestId manifest_id(source_name, module_uri);
  auto it = manifests_.find(manifest_id);
  if (it == manifests_.end()) {
    FXL_LOG(WARNING) << "Asked to remove non-existent manifest: "
                     << manifest_id;
    return;
  }

  const auto& manifest = it->second;
  for (const auto& intent_filter : *manifest.intent_filters) {
    action_to_manifests_[intent_filter.action].erase(manifest_id);

    for (const auto& constraint : intent_filter.parameter_constraints) {
      parameter_type_and_name_to_manifests_[std::make_pair(constraint.type,
                                                           constraint.name)]
          .erase(manifest_id);
      parameter_type_to_manifests_[constraint.type].erase(manifest_id);
    }
  }

  manifests_.erase(manifest_id);
}

void LocalModuleResolver::PeriodicCheckIfSourcesAreReady() {
  if (!AllSourcesAreReady()) {
    for (const auto& it : sources_) {
      if (ready_sources_.count(it.first) == 0) {
        FXL_LOG(WARNING) << "Still waiting on source: " << it.first;
      }
    }

    if (already_checking_if_sources_are_ready_) {
      return;
    }
    already_checking_if_sources_are_ready_ = true;
    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [weak_this = weak_factory_.GetWeakPtr()]() {
          if (weak_this) {
            weak_this->already_checking_if_sources_are_ready_ = false;
            weak_this->PeriodicCheckIfSourcesAreReady();
          }
        },
        zx::sec(10));
  }
}

}  // namespace modular
