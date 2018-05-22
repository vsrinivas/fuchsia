// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/module_resolver/local_module_resolver.h"

#include <algorithm>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/context/cpp/context_helper.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/strings/split_string.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/public/lib/entity/cpp/json.h"

namespace modular {

namespace {

// << operator for LocalModuleResolver::EntryId.
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

void LocalModuleResolver::AddSource(
    std::string name, std::unique_ptr<ModuleManifestSource> repo) {
  FXL_CHECK(bindings_.size() == 0);

  auto ptr = repo.get();
  sources_.emplace(name, std::move(repo));

  ptr->Watch(
      async_get_default(), [this, name]() { OnSourceIdle(name); },
      [this, name](std::string id, fuchsia::modular::ModuleManifest entry) {
        OnNewManifestEntry(name, std::move(id), std::move(entry));
      },
      [this, name](std::string id) {
        OnRemoveManifestEntry(name, std::move(id));
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
  void Run() {
    FlowToken flow{this, &response_};

    auto action_it =
        local_module_resolver_->action_to_entries_.find(query_.action);
    if (action_it == local_module_resolver_->action_to_entries_.end()) {
      response_ = CreateEmptyResponse();
      return;
    }

    candidates_ = action_it->second;

    // For each parameter in the FindModulesQuery, try to find Modules that
    // provide the types in the parameter as constraints.
    if (query_.parameter_constraints.is_null() ||
        query_.parameter_constraints->size() == 0) {
      Finally(flow);
      return;
    }

    for (const auto& parameter_entry : *query_.parameter_constraints) {
      ProcessParameterTypes(parameter_entry.param_name,
                            parameter_entry.param_types);
    }
    Finally(flow);
  }

 private:
  // |parameter_name| and |types| come from the FindModulesQuery.
  void ProcessParameterTypes(const std::string& parameter_name,
                             const fidl::VectorPtr<fidl::StringPtr>& types) {
    std::set<EntryId> parameter_type_entries;
    for (const auto& type : *types) {
      std::set<EntryId> found_entries =
          GetEntriesMatchingParameterByTypeAndName(type, parameter_name);
      parameter_type_entries.insert(found_entries.begin(), found_entries.end());
    }

    std::set<EntryId> new_result_entries;
    // All parameters in the query must be handled by the candidates. For each
    // parameter that is processed, filter out any existing results that can't
    // also handle the new parameter type.
    std::set_intersection(
        candidates_.begin(), candidates_.end(), parameter_type_entries.begin(),
        parameter_type_entries.end(),
        std::inserter(new_result_entries, new_result_entries.begin()));

    candidates_.swap(new_result_entries);
  }

  // Returns the EntryIds of all entries with a parameter that matches the
  // provided name and type.
  std::set<EntryId> GetEntriesMatchingParameterByTypeAndName(
      const std::string& parameter_type, const std::string& parameter_name) {
    std::set<EntryId> found_entries;
    auto found_entries_it =
        local_module_resolver_->parameter_type_and_name_to_entries_.find(
            std::make_pair(parameter_type, parameter_name));
    if (found_entries_it !=
        local_module_resolver_->parameter_type_and_name_to_entries_.end()) {
      found_entries.insert(found_entries_it->second.begin(),
                           found_entries_it->second.end());
    }
    return found_entries;
  }

  // At this point |candidates_| contains all the modules that could
  // potentially match the query. The purpose of this method is to create
  // those matches and populate |response_|.
  void Finally(FlowToken flow) {
    response_ = CreateEmptyResponse();
    if (candidates_.empty()) {
      return;
    }

    for (auto id : candidates_) {
      auto entry_it = local_module_resolver_->entries_.find(id);
      FXL_CHECK(entry_it != local_module_resolver_->entries_.end()) << id;

      const auto& entry = entry_it->second;
      fuchsia::modular::FindModulesResult result;
      result.module_id = entry.binary;
      result.manifest = fuchsia::modular::ModuleManifest::New();
      fidl::Clone(entry, result.manifest.get());

      response_.results.push_back(std::move(result));
    }
  }

  fuchsia::modular::FindModulesResponse CreateEmptyResponse() {
    fuchsia::modular::FindModulesResponse response;
    response.results.resize(0);
    return response;
  }

  fuchsia::modular::FindModulesResponse response_;
  LocalModuleResolver* const local_module_resolver_;
  fuchsia::modular::FindModulesQuery query_;

  std::set<EntryId> candidates_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FindModulesCall);
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

  void Run() {
    FlowToken flow{this, &response_};

    response_ = CreateEmptyResponse();

    std::set<EntryId> candidates;
    for (auto& constraint : *query_.parameter_constraints) {
      std::set<EntryId> param_type_entries;
      parameter_types_cache_[constraint.constraint_name] =
          constraint.param_types.Clone();
      for (auto& type : *constraint.param_types) {
        auto found_entries = GetEntriesMatchingParameterByType(type);
        candidates.insert(found_entries.begin(), found_entries.end());
      }
    }

    for (auto& candidate : candidates) {
      auto results = MatchQueryParametersToEntryParametersByType(
          local_module_resolver_->entries_[candidate]);

      using iter = decltype(results->begin());
      response_.results->insert(response_.results->end(),
                                std::move_iterator<iter>(results->begin()),
                                std::move_iterator<iter>(results->end()));
    }
  }

 private:
  fuchsia::modular::FindModulesByTypesResponse CreateEmptyResponse() {
    fuchsia::modular::FindModulesByTypesResponse r;
    r.results.resize(0);
    return r;
  }

  // Returns the set of all modules that have a parameter whose type is
  // |parameter_type|.
  std::set<LocalModuleResolver::EntryId> GetEntriesMatchingParameterByType(
      const std::string& parameter_type) {
    std::set<EntryId> found_entries;
    auto found_entries_it =
        local_module_resolver_->parameter_type_to_entries_.find(parameter_type);
    if (found_entries_it !=
        local_module_resolver_->parameter_type_to_entries_.end()) {
      found_entries.insert(found_entries_it->second.begin(),
                           found_entries_it->second.end());
    }
    return found_entries;
  }

  // Creates FindModulesResults for each available mapping from
  // parameters in |query_| to the corresponding parameters in each candidate
  // entry.
  //
  // In order for a query to match an entry, it must contain enough parameters
  // to populate each of the entry parameters.
  // TODO(MI4-866): Handle entries with optional parameters.
  fidl::VectorPtr<fuchsia::modular::FindModulesByTypesResult>
  MatchQueryParametersToEntryParametersByType(
      const fuchsia::modular::ModuleManifest& entry) {
    fidl::VectorPtr<fuchsia::modular::FindModulesByTypesResult> modules;
    modules.resize(0);
    if (query_.parameter_constraints->size() <
        entry.parameter_constraints->size()) {
      return modules;
    }

    // Map each parameter in |entry| to the query parameter names that could
    // be used to populate the |entry| parameter.
    std::map<std::string, std::vector<std::string>>
        entry_parameters_to_query_constraints =
            MapEntryParametersToCompatibleQueryParameters(entry);

    // Compute each possible map from |query_| parameter to the |entry|
    // parameter that it should populate.
    std::vector<std::map<std::string, std::string>> parameter_mappings =
        ComputeResultsFromEntryParameterToQueryParameterMapping(
            entry_parameters_to_query_constraints);

    // For each of the possible mappings, create a resolver result.
    for (const auto& parameter_mapping : parameter_mappings) {
      fuchsia::modular::FindModulesByTypesResult result;
      // TODO(vardhan): This score is a place holder. Compute a simple score for
      // results.
      result.score = 1.0f;
      result.module_id = entry.binary;
      result.action = entry.action;
      for (auto& kv : parameter_mapping) {
        fuchsia::modular::FindModulesByTypesParameterMapping entry;
        entry.query_constraint_name = kv.first;
        entry.result_param_name = kv.second;
        result.parameter_mappings.push_back(std::move(entry));
      }
      result.manifest = fuchsia::modular::ModuleManifest::New();
      fidl::Clone(entry, result.manifest.get());

      modules.push_back(std::move(result));
    }

    return modules;
  }

  // Returns a map where the keys are the |entry|'s parameters, and the values
  // are all the |query_| parameters that are type-compatible with that
  // |entry| parameter.
  std::map<std::string, std::vector<std::string>>
  MapEntryParametersToCompatibleQueryParameters(
      const fuchsia::modular::ModuleManifest& entry) {
    std::map<std::string, std::vector<std::string>>
        entry_parameter_to_query_constraints;
    for (const auto& entry_parameter : *entry.parameter_constraints) {
      std::vector<std::string> matching_query_constraints;
      for (const auto& query_constraint : *query_.parameter_constraints) {
        const auto& this_query_constraint_cache =
            parameter_types_cache_[query_constraint.constraint_name];
        if (std::find(this_query_constraint_cache->begin(),
                      this_query_constraint_cache->end(),
                      entry_parameter.type) !=
            this_query_constraint_cache->end()) {
          matching_query_constraints.push_back(
              query_constraint.constraint_name);
        }
      }
      entry_parameter_to_query_constraints[entry_parameter.name] =
          matching_query_constraints;
    }
    return entry_parameter_to_query_constraints;
  }

  // Returns a collection of valid mappings where the key is the query
  // parameter, and the value is the entry parameter to be populated with the
  // query parameters contents.
  //
  // |remaining_entry_parameters| are all the entry parameters that are yet to
  // be matched. |used_query_constraints| are all the query parameters that
  // have already been used in the current solution.
  std::vector<std::map<std::string, std::string>>
  ComputeResultsFromEntryParameterToQueryParameterMapping(
      const std::map<std::string, std::vector<std::string>>&
          remaining_entry_parameters,
      const std::set<std::string>& used_query_constraints = {}) {
    std::vector<std::map<std::string, std::string>> result;
    if (remaining_entry_parameters.empty()) {
      return result;
    }

    auto first_entry_parameter_it = remaining_entry_parameters.begin();
    const std::string& first_entry_parameter_name =
        first_entry_parameter_it->first;
    const std::vector<std::string> query_constraints_for_first_entry =
        first_entry_parameter_it->second;

    // If there is only one remaining entry parameter, create one result
    // mapping for each viable query parameter.
    if (remaining_entry_parameters.size() == 1) {
      for (const auto& query_constraint_name :
           query_constraints_for_first_entry) {
        // Don't create solutions where the query parameter has already been
        // used.
        if (used_query_constraints.find(query_constraint_name) !=
            used_query_constraints.end()) {
          continue;
        }

        std::map<std::string, std::string> result_map;
        result_map[query_constraint_name] = first_entry_parameter_name;
        result.push_back(result_map);
      }
      return result;
    }

    for (const auto& query_constraint_name : first_entry_parameter_it->second) {
      // If the query parameter has already been used, it cannot be matched
      // again, and thus the loop continues.
      if (used_query_constraints.find(query_constraint_name) !=
          used_query_constraints.end()) {
        continue;
      }

      // The current query parameter that will be used by the first entry
      // parameter must be added to the used set before computing the solution
      // to the smaller problem.
      std::set<std::string> new_used_query_constraints = used_query_constraints;
      new_used_query_constraints.insert(query_constraint_name);

      // Recurse for the remaining parameters.
      std::vector<std::map<std::string, std::string>> solution_for_remainder =
          ComputeResultsFromEntryParameterToQueryParameterMapping(
              {std::next(remaining_entry_parameters.begin()),
               remaining_entry_parameters.end()},
              new_used_query_constraints);

      // Expand each solution to the smaller problem by inserting the current
      // query parameter -> entry parameter into the solution.
      for (const auto& existing_solution : solution_for_remainder) {
        std::map<std::string, std::string> updated_solution = existing_solution;
        updated_solution[query_constraint_name] = first_entry_parameter_name;
        result.push_back(updated_solution);
      }
    }

    return result;
  }

  LocalModuleResolver* const local_module_resolver_;
  fuchsia::modular::FindModulesByTypesQuery const query_;
  fuchsia::modular::FindModulesByTypesResponse response_;
  // A cache of the parameter types for each parameter name in |query_|.
  std::map<std::string, fidl::VectorPtr<fidl::StringPtr>>
      parameter_types_cache_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FindModulesByTypesCall);
};

void LocalModuleResolver::FindModules(fuchsia::modular::FindModulesQuery query,
                                      FindModulesCallback callback) {
  FXL_DCHECK(!query.action.is_null());
  FXL_DCHECK(!query.parameter_constraints.is_null());

  operations_.Add(new FindModulesCall(this, std::move(query), callback));
}

void LocalModuleResolver::FindModulesByTypes(
    fuchsia::modular::FindModulesByTypesQuery query,
    FindModulesByTypesCallback callback) {
  FXL_DCHECK(!query.parameter_constraints.is_null());

  operations_.Add(new FindModulesByTypesCall(this, std::move(query), callback));
}

void LocalModuleResolver::GetModuleManifest(
    fidl::StringPtr module_id, GetModuleManifestCallback callback) {
  FXL_DCHECK(!module_id.is_null());

  for (auto& entry : entries_) {
    if (entry.first.second == module_id.get()) {
      callback(CloneOptional(entry.second));
      return;
    }
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
  auto proposals = fidl::VectorPtr<fuchsia::modular::Proposal>::New(0);
  if (query.text->empty()) {
    fuchsia::modular::QueryResponse response;
    response.proposals = std::move(proposals);
    done(std::move(response));
    return;
  }

  for (const auto& id_entry : entries_) {
    const auto& entry = id_entry.second;
    // Simply prefix match on the last element of the action.
    // actions have a convention of being namespaced like java classes:
    // com.google.subdomain.action
    std::string action = entry.action;
    auto parts =
        fxl::SplitString(action, ".", fxl::kKeepWhitespace, fxl::kSplitWantAll);
    const auto& last_part = parts.back();
    if (StringStartsWith(entry.action, query.text) ||
        StringStartsWith(last_part.ToString(), query.text)) {
      fuchsia::modular::Proposal proposal;
      proposal.id = entry.binary;

      fuchsia::modular::CreateStory create_story;
      create_story.intent.action.handler = entry.binary;

      fuchsia::modular::Action action;
      action.set_create_story(std::move(create_story));
      proposal.on_selected.push_back(std::move(action));

      proposal.display.headline =
          std::string("Go go gadget ") + last_part.ToString();
      proposal.display.subheadline = entry.binary;
      proposal.display.color = 0xffffffff;
      proposal.display.annoyance = fuchsia::modular::AnnoyanceType::NONE;

      proposal.confidence = 1.0;  // Yeah, super confident.

      proposals.push_back(std::move(proposal));
    }
  }

  if (proposals->size() > 10) {
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
    const std::string& source_name, std::string id_in,
    fuchsia::modular::ModuleManifest new_entry) {
  FXL_LOG(INFO) << "New Module manifest " << id_in
                << ": action = " << new_entry.action
                << ", binary = " << new_entry.binary;
  // Add this new entry info to our local index.
  if (entries_.count(EntryId(source_name, id_in)) > 0) {
    // Remove this existing entry first, then add it back in.
    OnRemoveManifestEntry(source_name, id_in);
  }
  auto ret =
      entries_.emplace(EntryId(source_name, id_in), std::move(new_entry));
  FXL_CHECK(ret.second);
  const auto& id = ret.first->first;
  const auto& entry = ret.first->second;
  action_to_entries_[entry.action].insert(id);

  for (const auto& constraint : *entry.parameter_constraints) {
    parameter_type_and_name_to_entries_[std::make_pair(constraint.type,
                                                       constraint.name)]
        .insert(id);
    parameter_type_to_entries_[constraint.type].insert(id);
  }
}

void LocalModuleResolver::OnRemoveManifestEntry(const std::string& source_name,
                                                std::string id_in) {
  EntryId id{source_name, id_in};
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    FXL_LOG(WARNING) << "Asked to remove non-existent manifest entry: " << id;
    return;
  }

  const auto& entry = it->second;
  action_to_entries_[entry.action].erase(id);

  for (const auto& constraint : *entry.parameter_constraints) {
    parameter_type_and_name_to_entries_[std::make_pair(constraint.type,
                                                       constraint.name)]
        .erase(id);
    parameter_type_to_entries_[constraint.type].erase(id);
  }

  entries_.erase(id);
}

void LocalModuleResolver::PeriodicCheckIfSourcesAreReady() {
  if (!AllSourcesAreReady()) {
    for (const auto& it : sources_) {
      if (ready_sources_.count(it.first) == 0) {
        FXL_LOG(WARNING) << "Still waiting on source: " << it.first;
      }
    }

    if (already_checking_if_sources_are_ready_)
      return;
    already_checking_if_sources_are_ready_ = true;
    async::PostDelayedTask(
        async_get_default(),
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
