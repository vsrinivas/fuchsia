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
#include "peridot/public/lib/entity/cpp/json.h"

namespace modular {

namespace {

// << operator for LocalModuleResolver::EntryId.
std::ostream& operator<<(std::ostream& o,
                         const std::pair<std::string, std::string>& id) {
  return o << id.first << ":" << id.second;
}

}  // namespace

LocalModuleResolver::LocalModuleResolver(
    fuchsia::modular::EntityResolverPtr entity_resolver)
    : query_handler_binding_(this),
      already_checking_if_sources_are_ready_(false),
      type_helper_(std::move(entity_resolver)),
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
    : public Operation<fuchsia::modular::FindModulesResult> {
 public:
  FindModulesCall(LocalModuleResolver* local_module_resolver,
                  fuchsia::modular::ResolverQuery query,
                  fuchsia::modular::ResolverScoringInfoPtr scoring_info,
                  ResultCall result_call)
      : Operation("LocalModuleResolver::FindModulesCall",
                  std::move(result_call)),
        local_module_resolver_(local_module_resolver),
        query_(std::move(query)),
        scoring_info_(std::move(scoring_info)) {}

  // Finds all modules that match |query_|.
  //
  // When an action is specified it is used to filter potential modules, and the
  // associated parameters are required to match in both name and type. If there
  // is no action, all modules are considered and only the parameter types are
  // used to filter results.
  void Run() {
    FlowToken flow{this, &result_};

    if (query_.handler) {
      // Client already knows what Module they want to use, so we'll
      // short-circuit resolution.
      result_ = HandleUrlQuery(query_);
      return;
    }

    if (query_.action) {
      auto action_it =
          local_module_resolver_->action_to_entries_.find(query_.action);
      if (action_it == local_module_resolver_->action_to_entries_.end()) {
        result_ = CreateEmptyResult();
        return;
      }

      candidates_ = action_it->second;
    }

    // For each parameter in the fuchsia::modular::ResolverQuery, try to find
    // Modules that provide the types in the parameter as constraints.
    if (query_.parameter_constraints.is_null() ||
        query_.parameter_constraints->size() == 0) {
      Finally(flow);
      return;
    }

    num_parameters_countdown_ = query_.parameter_constraints->size();
    for (const auto& parameter_entry : *query_.parameter_constraints) {
      const auto& parameter_name = parameter_entry.key;
      const auto& parameter_constraints = parameter_entry.constraint;

      local_module_resolver_->type_helper_.GetParameterTypes(
          parameter_constraints,
          [parameter_name, flow, this](std::vector<std::string> types) {
            ProcessParameterTypes(parameter_name, std::move(types),
                                  !query_.action.is_null());
            if (--num_parameters_countdown_ == 0) {
              Finally(flow);
            }
          });
    }
  }

 private:
  // |parameter_name| and |types| come from the fuchsia::modular::ResolverQuery.
  // |match_name| is true if the entries are required to match both the
  // parameter name and types. If false, only the types are matched.
  void ProcessParameterTypes(const std::string& parameter_name,
                             std::vector<std::string> types,
                             const bool query_contains_action) {
    parameter_types_cache_[parameter_name] = types;

    std::set<EntryId> parameter_type_entries;
    for (const auto& type : types) {
      std::set<EntryId> found_entries =
          query_contains_action
              ? GetEntriesMatchingParameterByTypeAndName(type, parameter_name)
              : GetEntriesMatchingParameterByType(type);
      parameter_type_entries.insert(found_entries.begin(), found_entries.end());
    }

    std::set<EntryId> new_result_entries;
    if (query_contains_action) {
      // If the query contains an action, the parameters are parameters to that
      // action and therefore all parameters in the query must be handled by the
      // candidates. For each parameter that is processed, filter out any
      // existing results that can't also handle the new parameter type.
      std::set_intersection(
          candidates_.begin(), candidates_.end(),
          parameter_type_entries.begin(), parameter_type_entries.end(),
          std::inserter(new_result_entries, new_result_entries.begin()));
    } else {
      // If the query does not contain an action, the search is widened to
      // include modules that are able to handle a proper subset of the query
      // parameters.
      std::set_union(
          candidates_.begin(), candidates_.end(),
          parameter_type_entries.begin(), parameter_type_entries.end(),
          std::inserter(new_result_entries, new_result_entries.begin()));
    }
    candidates_.swap(new_result_entries);
  }

  // Returns the EntryIds of all entries with a parameter that matches the
  // provided type.
  std::set<EntryId> GetEntriesMatchingParameterByType(
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

  // At this point |candidates_| contains all the modules that could potentially
  // match the query. The purpose of this method is to create those matches and
  // populate |result_|.
  void Finally(FlowToken flow) {
    result_ = CreateEmptyResult();
    if (candidates_.empty()) {
      return;
    }

    // For each of the potential candidates, compute each possible mapping from
    // query parameter to entry parameter. If the query specifies an action or
    // explicit handler there is only one result per candidate. If there is no
    // action or handler, results will be constructed for each mapping from
    // entry parameter to |query_| parameter where the types are compatible.
    for (auto id : candidates_) {
      auto entry_it = local_module_resolver_->entries_.find(id);
      FXL_CHECK(entry_it != local_module_resolver_->entries_.end()) << id;

      const auto& entry = entry_it->second;

      if (!query_.action && !query_.handler) {
        // If there is no action and no explicit handler each entry may be able
        // to be matched in multiple ways (i.e. values of parameters with the
        // same type are interchangeable), so the result set may contain
        // multiple entries for the same candidate.
        auto new_results = MatchQueryParametersToEntryParametersByType(entry);
        for (size_t i = 0; i < new_results->size(); ++i) {
          result_.modules.push_back(std::move(new_results->at(i)));
        }

      } else {
        fuchsia::modular::ModuleResolverResult result;
        result.module_id = entry.binary;
        result.manifest = fuchsia::modular::ModuleManifest::New();
        fidl::Clone(entry, result.manifest.get());
        CopyParametersToModuleResolverResult(query_, &result);

        result_.modules.push_back(std::move(result));
      }
    }
  }

  // Creates ModuleResolverResultPtrs for each available mapping from parameters
  // in |query_| to the corresponding parameters in each candidate entry.
  //
  // In order for a query to match an entry, it must contain enough parameters
  // to populate each of the entry parameters.
  fidl::VectorPtr<fuchsia::modular::ModuleResolverResult>
  MatchQueryParametersToEntryParametersByType(
      const fuchsia::modular::ModuleManifest& entry) {
    fidl::VectorPtr<fuchsia::modular::ModuleResolverResult> modules;
    modules.resize(0);
    // TODO(MI4-866): Handle entries with optional parameters.
    if (query_.parameter_constraints->size() <
        entry.parameter_constraints->size()) {
      return modules;
    }

    // Map each parameter in |entry| to the query parameter names that could be
    // used to populate the |entry| parameter.
    std::map<std::string, std::vector<std::string>>
        entry_parameters_to_query_parameters =
            MapEntryParametersToCompatibleQueryParameters(entry);

    // Compute each possible map from |query_| parameter to the |entry|
    // parameter that it should populate.
    std::vector<std::map<std::string, std::string>> parameter_mappings =
        ComputeResultsFromEntryParameterToQueryParameterMapping(
            entry_parameters_to_query_parameters);

    // For each of the possible mappings, create a resolver result.
    for (const auto& parameter_mapping : parameter_mappings) {
      fuchsia::modular::ModuleResolverResult result;
      result.module_id = entry.binary;
      result.manifest = fuchsia::modular::ModuleManifest::New();
      fidl::Clone(entry, result.manifest.get());
      CopyParametersToModuleResolverResult(query_, &result, parameter_mapping);
      modules.push_back(std::move(result));
    }

    return modules;
  }

  // Returns a map where the keys are the |entry|'s parameters, and the values
  // are all the |query_| parameters that are type-compatible with that |entry|
  // parameter.
  std::map<std::string, std::vector<std::string>>
  MapEntryParametersToCompatibleQueryParameters(
      const fuchsia::modular::ModuleManifest& entry) {
    std::map<std::string, std::vector<std::string>>
        entry_parameter_to_query_parameters;
    for (const auto& entry_parameter : *entry.parameter_constraints) {
      std::vector<std::string> matching_query_parameters;
      for (const auto& query_parameter : *query_.parameter_constraints) {
        const auto& this_query_parameter_cache =
            parameter_types_cache_[query_parameter.key];
        if (std::find(this_query_parameter_cache.begin(),
                      this_query_parameter_cache.end(), entry_parameter.type) !=
            this_query_parameter_cache.end()) {
          matching_query_parameters.push_back(query_parameter.key);
        }
      }
      entry_parameter_to_query_parameters[entry_parameter.name] =
          matching_query_parameters;
    }
    return entry_parameter_to_query_parameters;
  }

  // Returns a collection of valid mappings where the key is the query
  // parameter, and the value is the entry parameter to be populated with the
  // query parameters contents.
  //
  // |remaining_entry_parameters| are all the entry parameters that are yet to
  // be matched. |used_query_parameters| are all the query parameters that have
  // already been used in the current solution.
  std::vector<std::map<std::string, std::string>>
  ComputeResultsFromEntryParameterToQueryParameterMapping(
      const std::map<std::string, std::vector<std::string>>&
          remaining_entry_parameters,
      const std::set<std::string>& used_query_parameters = {}) {
    std::vector<std::map<std::string, std::string>> result;
    if (remaining_entry_parameters.empty()) {
      return result;
    }

    auto first_entry_parameter_it = remaining_entry_parameters.begin();
    const std::string& first_entry_parameter_name =
        first_entry_parameter_it->first;
    const std::vector<std::string> query_parameters_for_first_entry =
        first_entry_parameter_it->second;

    // If there is only one remaining entry parameter, create one result mapping
    // for each viable query parameter.
    if (remaining_entry_parameters.size() == 1) {
      for (const auto& query_parameter_name :
           query_parameters_for_first_entry) {
        // Don't create solutions where the query parameter has already been
        // used.
        if (used_query_parameters.find(query_parameter_name) !=
            used_query_parameters.end()) {
          continue;
        }

        std::map<std::string, std::string> result_map;
        result_map[query_parameter_name] = first_entry_parameter_name;
        result.push_back(result_map);
      }
      return result;
    }

    for (const auto& query_parameter_name : first_entry_parameter_it->second) {
      // If the query parameter has already been used, it cannot be matched
      // again, and thus the loop continues.
      if (used_query_parameters.find(query_parameter_name) !=
          used_query_parameters.end()) {
        continue;
      }

      // The current query parameter that will be used by the first entry
      // parameter must be added to the used set before computing the solution
      // to the smaller problem.
      std::set<std::string> new_used_query_parameters = used_query_parameters;
      new_used_query_parameters.insert(query_parameter_name);

      // Recurse for the remaining parameters.
      std::vector<std::map<std::string, std::string>> solution_for_remainder =
          ComputeResultsFromEntryParameterToQueryParameterMapping(
              {std::next(remaining_entry_parameters.begin()),
               remaining_entry_parameters.end()},
              new_used_query_parameters);

      // Expand each solution to the smaller problem by inserting the current
      // query parameter -> entry parameter into the solution.
      for (const auto& existing_solution : solution_for_remainder) {
        std::map<std::string, std::string> updated_solution = existing_solution;
        updated_solution[query_parameter_name] = first_entry_parameter_name;
        result.push_back(updated_solution);
      }
    }

    return result;
  }

  std::vector<std::string> ToArray(fidl::VectorPtr<fidl::StringPtr>& values) {
    std::vector<std::string> ret;
    for (fidl::StringPtr str : *values) {
      ret.push_back(str.get());
    }
    return ret;
  }

  // Copies the parameters from |query| to the provided resolver result.
  //
  // |parameter_mapping| A mapping from the parameter names of query to the
  // entry parameter that should be populated. If no such mapping exists, the
  // query parameter name will be used.
  void CopyParametersToModuleResolverResult(
      const fuchsia::modular::ResolverQuery& query,
      fuchsia::modular::ModuleResolverResult* result,
      std::map<std::string, std::string> parameter_mapping = {}) {
    auto& create_param_map_info =
        result->create_parameter_map_info;  // For convenience.
    for (const auto& query_parameter : *query.parameter_constraints) {
      const auto& parameter = query_parameter.constraint;
      fidl::StringPtr name = query_parameter.key;
      auto it = parameter_mapping.find(query_parameter.key);
      if (it != parameter_mapping.end()) {
        name = it->second;
      }

      if (parameter.is_entity_reference()) {
        fuchsia::modular::CreateLinkInfo create_link;
        create_link.initial_data =
            EntityReferenceToJson(parameter.entity_reference());
        // TODO(thatguy): set |create_link->allowed_types|.
        // TODO(thatguy): set |create_link->permissions|.
        fuchsia::modular::CreateModuleParameterInfo property_info;
        property_info.set_create_link(std::move(create_link));
        fuchsia::modular::CreateModuleParameterMapEntry entry;
        entry.key = name;
        entry.value = std::move(property_info);
        create_param_map_info.property_info.push_back(std::move(entry));
      } else if (parameter.is_link_info()) {
        fuchsia::modular::CreateModuleParameterInfo property_info;
        fuchsia::modular::LinkPath link_path;
        parameter.link_info().path.Clone(&link_path);
        property_info.set_link_path(std::move(link_path));
        fuchsia::modular::CreateModuleParameterMapEntry entry;
        entry.key = name;
        entry.value = std::move(property_info);
        create_param_map_info.property_info.push_back(std::move(entry));
      } else if (parameter.is_json()) {
        fuchsia::modular::CreateLinkInfo create_link;
        create_link.initial_data = parameter.json();
        // TODO(thatguy): set |create_link->allowed_types|.
        // TODO(thatguy): set |create_link->permissions|.
        fuchsia::modular::CreateModuleParameterInfo property_info;
        property_info.set_create_link(std::move(create_link));
        fuchsia::modular::CreateModuleParameterMapEntry entry;
        entry.key = name;
        entry.value = std::move(property_info);
        create_param_map_info.property_info.push_back(std::move(entry));
      }
      // There's nothing to copy over from 'entity_types', since it only
      // specifies parameter constraint information, and no actual content.
    }
  }

  fuchsia::modular::FindModulesResult HandleUrlQuery(
      const fuchsia::modular::ResolverQuery& query) {
    fuchsia::modular::ModuleResolverResult mod_result;
    mod_result.module_id = query.handler;
    for (const auto& iter : local_module_resolver_->entries_) {
      if (iter.second.binary == query.handler) {
        mod_result.manifest = fuchsia::modular::ModuleManifest::New();
        fidl::Clone(iter.second, mod_result.manifest.get());
      }
    }

    CopyParametersToModuleResolverResult(query, &mod_result);

    fuchsia::modular::FindModulesResult result;
    result.modules.push_back(std::move(mod_result));
    return result;
  }

  fuchsia::modular::FindModulesResult CreateEmptyResult() {
    fuchsia::modular::FindModulesResult result;
    result.modules.resize(0);
    return result;
  }

  fuchsia::modular::FindModulesResult result_;

  LocalModuleResolver* const local_module_resolver_;
  fuchsia::modular::ResolverQuery query_;
  fuchsia::modular::ResolverScoringInfoPtr scoring_info_;

  // A cache of the fuchsia::modular::Entity types for each parameter in
  // |query_|.
  std::map<std::string, std::vector<std::string>> parameter_types_cache_;

  std::set<EntryId> candidates_;
  uint32_t num_parameters_countdown_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(FindModulesCall);
};

void LocalModuleResolver::FindModules(fuchsia::modular::ResolverQuery query,
                                      FindModulesCallback done) {
  operations_.Add(new FindModulesCall(this, std::move(query), nullptr, done));
}

void LocalModuleResolver::FindModules(
    fuchsia::modular::ResolverQuery query,
    fuchsia::modular::ResolverScoringInfoPtr scoring_info,
    FindModulesCallback done) {
  operations_.Add(new FindModulesCall(this, std::move(query),
                                      std::move(scoring_info), done));
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
      fuchsia::modular::Intent intent;
      intent.action.handler = entry.binary;
      create_story.intent = std::move(intent);

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
