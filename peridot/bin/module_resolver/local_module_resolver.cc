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
    : query_handler_binding_(this),
      already_checking_if_sources_are_ready_(false),
      weak_factory_(this) {}

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

void LocalModuleResolver::BindQueryHandler(
    fidl::InterfaceRequest<fuchsia::modular::QueryHandler> request) {
  query_handler_binding_.Bind(std::move(request));
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

class LocalModuleResolver::FindModulesByTypesCall
    : public Operation<fuchsia::modular::FindModulesByTypesResponse> {
 public:
  FindModulesByTypesCall(LocalModuleResolver* const local_module_resolver,
                         fuchsia::modular::FindModulesByTypesQuery query,
                         ResultCall result_call)
      : Operation("LocalModuleResolver::FindModulesByTypesCall",
                  std::move(result_call)),
        local_module_resolver_(local_module_resolver),
        query_(std::move(query)) {}

  void Run() override {
    FlowToken flow{this, &response_};

    response_ = CreateEmptyResponseWithStatus();

    std::set<ManifestId> candidates;
    for (auto& constraint : query_.parameter_constraints) {
      std::set<ManifestId> param_type_entries;
      parameter_types_cache_[constraint.constraint_name] =
          constraint.param_types;
      for (auto& type : constraint.param_types) {
        auto found_entries = GetManifestsMatchingParameterByType(type);
        candidates.insert(found_entries.begin(), found_entries.end());
      }
    }

    for (auto& candidate : candidates) {
      auto results = MatchQueryParametersToManifestParametersByType(
          local_module_resolver_->manifests_[candidate]);

      using iter = decltype(results->begin());
      response_.results.insert(response_.results.end(),
                               std::move_iterator<iter>(results->begin()),
                               std::move_iterator<iter>(results->end()));
    }
  }

 private:
  fuchsia::modular::FindModulesByTypesResponse CreateEmptyResponseWithStatus() {
    fuchsia::modular::FindModulesByTypesResponse r;
    r.results.resize(0);
    return r;
  }

  // Returns the set of all modules that have a parameter whose type is
  // |parameter_type|.
  std::set<LocalModuleResolver::ManifestId> GetManifestsMatchingParameterByType(
      const std::string& parameter_type) {
    std::set<ManifestId> found_entries;
    auto found_manifests_it =
        local_module_resolver_->parameter_type_to_manifests_.find(
            parameter_type);
    if (found_manifests_it !=
        local_module_resolver_->parameter_type_to_manifests_.end()) {
      found_entries.insert(found_manifests_it->second.begin(),
                           found_manifests_it->second.end());
    }
    return found_entries;
  }

  // Creates FindModulesResults for each available mapping from
  // parameters in |query_| to the corresponding parameters in each candidate
  // manifest.
  //
  // In order for a query to match an manifest, it must contain enough
  // parameters to populate each of the manifest parameters.
  // TODO(MI4-866): Handle entries with optional parameters.
  fidl::VectorPtr<fuchsia::modular::FindModulesByTypesResult>
  MatchQueryParametersToManifestParametersByType(
      const fuchsia::modular::ModuleManifest& manifest) {
    fidl::VectorPtr<fuchsia::modular::FindModulesByTypesResult> modules;
    modules.resize(0);

    for (const auto& intent_filter : *manifest.intent_filters) {
      if (query_.parameter_constraints.size() <
          intent_filter.parameter_constraints.size()) {
        return modules;
      }

      // Map each parameter in |intent_filter| to the query parameter names
      // that could be used to populate the |intent_filter| parameter.
      std::map<ParameterName, std::vector<ParameterName>>
          intent_filter_params_to_query_constraints =
              MapManifestParametersToCompatibleQueryParameters(intent_filter);

      // Compute each possible map from |query_| parameter to the
      // |intent_filter| parameter that it should populate.
      std::vector<std::map<ParameterName, ParameterName>> parameter_mappings =
          ComputeResultsFromEntryParameterToQueryParameterMapping(
              intent_filter_params_to_query_constraints);

      // For each of the possible mappings, create a resolver result.
      for (const auto& parameter_mapping : parameter_mappings) {
        fuchsia::modular::FindModulesByTypesResult result;
        // TODO(vardhan): This score is a place holder. Compute a simple score
        // for results.
        result.score = 1.0f;
        result.module_id = manifest.binary;
        result.action = intent_filter.action;
        for (auto& kv : parameter_mapping) {
          fuchsia::modular::FindModulesByTypesParameterMapping mapping;
          mapping.query_constraint_name = kv.first;
          mapping.result_param_name = kv.second;
          result.parameter_mappings.push_back(std::move(mapping));
        }
        result.manifest = fuchsia::modular::ModuleManifest::New();
        fidl::Clone(manifest, result.manifest.get());

        modules.push_back(std::move(result));
      }
    }

    return modules;
  }

  // Returns a map where the keys are the |intent_filter|'s parameter names,
  // and the values are all the |query_| parameter names that are
  // type-compatible with that |intent_filter| parameter name.
  std::map<std::string, std::vector<std::string>>
  MapManifestParametersToCompatibleQueryParameters(
      const fuchsia::modular::IntentFilter& intent_filter) {
    std::map<ParameterName, std::vector<ParameterName>>
        intent_filter_param_to_query_constraints;
    for (const auto& intent_filter_param :
         intent_filter.parameter_constraints) {
      std::vector<ParameterName> matching_query_constraints;
      for (const auto& query_constraint : query_.parameter_constraints) {
        const auto& this_query_constraint_cache =
            parameter_types_cache_[query_constraint.constraint_name];
        if (std::find(this_query_constraint_cache.begin(),
                      this_query_constraint_cache.end(),
                      intent_filter_param.type) !=
            this_query_constraint_cache.end()) {
          matching_query_constraints.push_back(
              query_constraint.constraint_name);
        }
      }
      intent_filter_param_to_query_constraints[intent_filter_param.name] =
          matching_query_constraints;
    }
    return intent_filter_param_to_query_constraints;
  }

  // Returns a collection of valid mappings where the key is the query
  // parameter, and the value is the manifest parameter to be populated with
  // the query parameters contents.
  //
  // |remaining_intent_filter_params| are all the manifest parameters that are
  // yet to be matched. |used_query_constraints| are all the query parameters
  // that have already been used in the current solution.
  std::vector<std::map<std::string, std::string>>
  ComputeResultsFromEntryParameterToQueryParameterMapping(
      const std::map<std::string, std::vector<std::string>>&
          remaining_intent_filter_params,
      const std::set<std::string>& used_query_constraints = {}) {
    std::vector<std::map<std::string, std::string>> result;
    if (remaining_intent_filter_params.empty()) {
      return result;
    }

    auto first_intent_filter_param_it = remaining_intent_filter_params.begin();
    const std::string& first_intent_filter_param_name =
        first_intent_filter_param_it->first;
    const std::vector<std::string> query_constraints_for_first_entry =
        first_intent_filter_param_it->second;

    // If there is only one remaining manifest parameter, create one result
    // mapping for each viable query parameter.
    if (remaining_intent_filter_params.size() == 1) {
      for (const auto& query_constraint_name :
           query_constraints_for_first_entry) {
        // Don't create solutions where the query parameter has already been
        // used.
        if (used_query_constraints.find(query_constraint_name) !=
            used_query_constraints.end()) {
          continue;
        }

        std::map<std::string, std::string> result_map;
        result_map[query_constraint_name] = first_intent_filter_param_name;
        result.push_back(result_map);
      }
      return result;
    }

    for (const auto& query_constraint_name :
         first_intent_filter_param_it->second) {
      // If the query parameter has already been used, it cannot be matched
      // again, and thus the loop continues.
      if (used_query_constraints.find(query_constraint_name) !=
          used_query_constraints.end()) {
        continue;
      }

      // The current query parameter that will be used by the first manifest
      // parameter must be added to the used set before computing the solution
      // to the smaller problem.
      std::set<std::string> new_used_query_constraints = used_query_constraints;
      new_used_query_constraints.insert(query_constraint_name);

      // Recurse for the remaining parameters.
      std::vector<std::map<std::string, std::string>> solution_for_remainder =
          ComputeResultsFromEntryParameterToQueryParameterMapping(
              {std::next(remaining_intent_filter_params.begin()),
               remaining_intent_filter_params.end()},
              new_used_query_constraints);

      // Expand each solution to the smaller problem by inserting the current
      // query parameter -> manifest parameter into the solution.
      for (const auto& existing_solution : solution_for_remainder) {
        std::map<std::string, std::string> updated_solution = existing_solution;
        updated_solution[query_constraint_name] =
            first_intent_filter_param_name;
        result.push_back(updated_solution);
      }
    }

    return result;
  }

  LocalModuleResolver* const local_module_resolver_;
  fuchsia::modular::FindModulesByTypesQuery const query_;
  fuchsia::modular::FindModulesByTypesResponse response_;
  // A cache of the parameter types for each parameter name in |query_|.
  std::map<std::string, std::vector<std::string>> parameter_types_cache_;
};

void LocalModuleResolver::FindModules(fuchsia::modular::FindModulesQuery query,
                                      FindModulesCallback callback) {
  operations_.Add(std::make_unique<FindModulesCall>(this, std::move(query),
                                                    std::move(callback)));
}

void LocalModuleResolver::FindModulesByTypes(
    fuchsia::modular::FindModulesByTypesQuery query,
    FindModulesByTypesCallback callback) {
  operations_.Add(std::make_unique<FindModulesByTypesCall>(
      this, std::move(query), std::move(callback)));
}

void LocalModuleResolver::GetModuleManifest(
    std::string module_id, GetModuleManifestCallback callback) {
  auto found_handlers = FindHandlers(module_id);
  if (!found_handlers.empty()) {
    callback(CloneOptional(manifests_[*found_handlers.begin()]));
    return;
  }

  callback(nullptr);
}

namespace {
bool StringStartsWith(const std::string& str, const std::string& prefix) {
  return str.compare(0, prefix.length(), prefix) == 0;
}
}  // namespace

void LocalModuleResolver::OnQuery(fuchsia::modular::UserInput query,
                                  OnQueryCallback done) {
  // TODO(thatguy): This implementation is bare-bones. Don't judge.
  // Before adding new member variables to support OnQuery() (and tying the
  // LocalModuleResolver internals up with what's needed for this method),
  // please split the index-building & querying portion of LocalModuleResolver
  // out into its own class. Then, make a new class to handle OnQuery() and
  // share the same index instance here and there.
  std::vector<fuchsia::modular::Proposal> proposals;
  if (query.text.empty()) {
    fuchsia::modular::QueryResponse response;
    response.proposals = std::move(proposals);
    done(std::move(response));
    return;
  }

  for (const auto& id_entry : manifests_) {
    const auto& manifest = id_entry.second;
    for (const auto& intent_filter : *manifest.intent_filters) {
      // Simply prefix match on the last element of the action.
      // actions have a convention of being namespaced like java classes:
      // com.google.subdomain.action
      std::string action = intent_filter.action;
      auto parts = fxl::SplitString(action, ".", fxl::kKeepWhitespace,
                                    fxl::kSplitWantAll);
      const auto& last_part = parts.back();
      if (StringStartsWith(intent_filter.action, query.text) ||
          StringStartsWith(last_part.ToString(), query.text)) {
        fuchsia::modular::Proposal proposal;
        proposal.id = manifest.binary;
        proposal.affinity.resize(0);

        fuchsia::modular::AddMod add_mod;
        add_mod.intent.handler = manifest.binary;
        add_mod.mod_name_transitional = "root";

        fuchsia::modular::StoryCommand command;
        command.set_add_mod(std::move(add_mod));
        proposal.on_selected.push_back(std::move(command));

        proposal.display.headline =
            std::string("Go go gadget ") + last_part.ToString();
        proposal.display.subheadline = manifest.binary;
        proposal.display.color = 0xffffffff;
        proposal.display.annoyance = fuchsia::modular::AnnoyanceType::NONE;

        proposal.confidence = 1.0;  // Yeah, super confident.

        proposals.push_back(std::move(proposal));
      }
    }
  }

  if (proposals.size() > 10) {
    proposals.resize(10);
  }

  fuchsia::modular::QueryResponse response;
  response.proposals = std::move(proposals);
  done(std::move(response));
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
