// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "peridot/bin/module_resolver/module_resolver_impl.h"

#include "garnet/public/lib/fxl/strings/split_string.h"
#include "lib/context/cpp/context_helper.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/public/lib/entity/cpp/json.h"
#include "peridot/public/lib/story/fidl/create_chain.fidl.h"

namespace maxwell {

namespace {

// << operator for ModuleResolverImpl::EntryId.
std::ostream& operator<<(std::ostream& o,
                         const std::pair<std::string, std::string>& id) {
  return o << id.first << ":" << id.second;
}

}  // namespace

ModuleResolverImpl::ModuleResolverImpl(
    modular::EntityResolverPtr entity_resolver)
    : query_handler_binding_(this),
      already_checking_if_sources_are_ready_(false),
      type_helper_(std::move(entity_resolver)),
      weak_factory_(this) {}

ModuleResolverImpl::~ModuleResolverImpl() = default;

void ModuleResolverImpl::AddSource(
    std::string name,
    std::unique_ptr<modular::ModuleManifestSource> repo) {
  FXL_CHECK(bindings_.size() == 0);

  auto ptr = repo.get();
  sources_.emplace(name, std::move(repo));

  ptr->Watch(
      fsl::MessageLoop::GetCurrent()->task_runner(),
      [this, name]() { OnSourceIdle(name); },
      [this, name](std::string id, const modular::ModuleManifestPtr& entry) {
        OnNewManifestEntry(name, std::move(id), entry.Clone());
      },
      [this, name](std::string id) {
        OnRemoveManifestEntry(name, std::move(id));
      });
}

void ModuleResolverImpl::Connect(
    f1dl::InterfaceRequest<modular::ModuleResolver> request) {
  if (!AllSourcesAreReady()) {
    PeriodicCheckIfSourcesAreReady();
    pending_bindings_.push_back(std::move(request));
  } else {
    bindings_.AddBinding(this, std::move(request));
  }
}

void ModuleResolverImpl::BindQueryHandler(
    f1dl::InterfaceRequest<QueryHandler> request) {
  query_handler_binding_.Bind(std::move(request));
}

class ModuleResolverImpl::FindModulesCall
    : modular::Operation<modular::FindModulesResultPtr> {
 public:
  FindModulesCall(modular::OperationContainer* container,
                  ModuleResolverImpl* module_resolver_impl,
                  modular::ResolverQueryPtr query,
                  modular::ResolverScoringInfoPtr scoring_info,
                  ResultCall result_call)
      : Operation("ModuleResolverImpl::FindModulesCall",
                  container,
                  std::move(result_call)),
        module_resolver_impl_(module_resolver_impl),
        query_(std::move(query)),
        scoring_info_(std::move(scoring_info)) {
    Ready();
  }

  // Finds all modules that match |query_|.
  //
  // When a verb is specified it is used to filter potential modules, and the
  // associated nouns are required to match in both name and type. If there is
  // no verb, all modules are considered and only the noun types are used to
  // filter results.
  void Run() {
    FlowToken flow{this, &result_};

    if (query_->url) {
      // TODO(MI4-888): revisit this short circuiting and add the module
      // manifest to the result.
      // Client already knows what Module they want to use, so we'll
      // short-circuit resolution.
      result_ = HandleUrlQuery(query_);
      return;
    }

    if (query_->verb) {
      auto verb_it = module_resolver_impl_->verb_to_entries_.find(query_->verb);
      if (verb_it == module_resolver_impl_->verb_to_entries_.end()) {
        result_ = CreateEmptyResult();
        return;
      }

      candidates_ = verb_it->second;
    }

    // For each noun in the ResolverQuery, try to find Modules that provide the
    // types in the noun as constraints.
    if (query_->noun_constraints.is_null() ||
        query_->noun_constraints->size() == 0) {
      Finally(flow);
      return;
    }

    num_nouns_countdown_ = query_->noun_constraints->size();
    for (const auto& noun_entry : *query_->noun_constraints) {
      const auto& noun_name = noun_entry->key;
      const auto& noun_constraints = noun_entry->constraint;

      module_resolver_impl_->type_helper_.GetNounTypes(
          noun_constraints,
          [noun_name, flow, this](std::vector<std::string> types) {
            ProcessNounTypes(noun_name, std::move(types),
                             !query_->verb.is_null());
            if (--num_nouns_countdown_ == 0) {
              Finally(flow);
            }
          });
    }
  }

 private:
  // |noun_name| and |types| come from the ResolverQuery.
  // |match_name| is true if the entries are required to match both the noun
  // name and types. If false, only the types are matched.
  void ProcessNounTypes(const std::string& noun_name,
                        std::vector<std::string> types,
                        const bool query_contains_verb) {
    noun_types_cache_[noun_name] = types;

    std::set<EntryId> noun_type_entries;
    for (const auto& type : types) {
      std::set<EntryId> found_entries =
          query_contains_verb
              ? GetEntriesMatchingNounByTypeAndName(type, noun_name)
              : GetEntriesMatchingNounByType(type);
      noun_type_entries.insert(found_entries.begin(), found_entries.end());
    }

    std::set<EntryId> new_result_entries;
    if (query_contains_verb) {
      // If the query contains a verb, the nouns are parameters to that verb and
      // therefore all nouns in the query must be handled by the candidates. For
      // each noun that is processed, filter out any existing results that can't
      // also handle the new noun type.
      std::set_intersection(
          candidates_.begin(), candidates_.end(), noun_type_entries.begin(),
          noun_type_entries.end(),
          std::inserter(new_result_entries, new_result_entries.begin()));
    } else {
      // If the query does not contain a verb, the search is widened to include
      // modules that are able to handle a proper subset of the query nouns.
      std::set_union(
          candidates_.begin(), candidates_.end(), noun_type_entries.begin(),
          noun_type_entries.end(),
          std::inserter(new_result_entries, new_result_entries.begin()));
    }
    candidates_.swap(new_result_entries);
  }

  // Returns the EntryIds of all entries with a noun that matches the provided
  // type.
  std::set<EntryId> GetEntriesMatchingNounByType(const std::string& noun_type) {
    std::set<EntryId> found_entries;
    auto found_entries_it =
        module_resolver_impl_->noun_type_to_entries_.find(noun_type);
    if (found_entries_it !=
        module_resolver_impl_->noun_type_to_entries_.end()) {
      found_entries.insert(found_entries_it->second.begin(),
                           found_entries_it->second.end());
    }
    return found_entries;
  }

  // Returns the EntryIds of all entries with a noun that matches the provided
  // name and type.
  std::set<EntryId> GetEntriesMatchingNounByTypeAndName(
      const std::string& noun_type,
      const std::string& noun_name) {
    std::set<EntryId> found_entries;
    auto found_entries_it =
        module_resolver_impl_->noun_type_and_name_to_entries_.find(
            std::make_pair(noun_type, noun_name));
    if (found_entries_it !=
        module_resolver_impl_->noun_type_and_name_to_entries_.end()) {
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
    // query noun to entry noun. If the query specifies a verb or url, there is
    // only one result per candidate. If there is no verb or url, results will
    // be constructed for each mapping from entry noun to |query_| noun where
    // the types are compatible.
    for (auto id : candidates_) {
      auto entry_it = module_resolver_impl_->entries_.find(id);
      FXL_CHECK(entry_it != module_resolver_impl_->entries_.end()) << id;

      const auto& entry = entry_it->second;

      if (!query_->verb && !query_->url) {
        // If there is no verb and no url each entry may be able to be matched
        // in multiple ways (i.e. values of nouns with the same type are
        // interchangeable), so the result set may contain multiple entries for
        // the same candidate.
        auto new_results = MatchQueryNounsToEntryNounsByType(entry);
        for (size_t i = 0; i < new_results->size(); ++i) {
          result_->modules.push_back(std::move(new_results->at(i)));
        }

      } else {
        auto result = modular::ModuleResolverResult::New();
        result->module_id = entry->binary;
        result->manifest = entry.Clone();
        CopyNounsToModuleResolverResult(query_, result.get());

        result_->modules.push_back(std::move(result));
      }
    }
  }

  // Creates ModuleResolverResultPtrs for each available mapping from nouns in
  // |query_| to the corresponding nouns in each candidate entry.
  //
  // In order for a query to match an entry, it must contain enough nouns to
  // populate each of the entry nouns.
  f1dl::VectorPtr<modular::ModuleResolverResultPtr>
  MatchQueryNounsToEntryNounsByType(const modular::ModuleManifestPtr& entry) {
    f1dl::VectorPtr<modular::ModuleResolverResultPtr> modules;
    modules.resize(0);
    // TODO(MI4-866): Handle entries with optional nouns.
    if (query_->noun_constraints->size() < entry->noun_constraints->size()) {
      return modules;
    }

    // Map each noun in |entry| to the query noun names that could be used to
    // populate the |entry| noun.
    std::map<std::string, std::vector<std::string>> entry_nouns_to_query_nouns =
        MapEntryNounsToCompatibleQueryNouns(entry);

    // Compute each possible map from |query_| noun to the |entry| noun that it
    // should populate.
    std::vector<std::map<std::string, std::string>> noun_mappings =
        ComputeResultsFromEntryNounToQueryNounMapping(
            entry_nouns_to_query_nouns);

    // For each of the possible mappings, create a resolver result.
    for (const auto& noun_mapping : noun_mappings) {
      auto result = modular::ModuleResolverResult::New();
      result->module_id = entry->binary;
      result->manifest = entry.Clone();
      CopyNounsToModuleResolverResult(query_, result.get(), noun_mapping);
      modules.push_back(std::move(result));
    }

    return modules;
  }

  // Returns a map where the keys are the |entry|'s nouns, and the values are
  // all the |query_| nouns that are type-compatible with that |entry| noun.
  std::map<std::string, std::vector<std::string>>
  MapEntryNounsToCompatibleQueryNouns(const modular::ModuleManifestPtr& entry) {
    std::map<std::string, std::vector<std::string>> entry_noun_to_query_nouns;
    for (const auto& entry_noun : *entry->noun_constraints) {
      std::vector<std::string> matching_query_nouns;
      for (const auto& query_noun : *query_->noun_constraints) {
        std::vector<std::string> entry_noun_types = ToArray(entry_noun->types);
        if (DoTypesIntersect(noun_types_cache_[query_noun->key],
                             entry_noun_types)) {
          matching_query_nouns.push_back(query_noun->key);
        }
      }
      entry_noun_to_query_nouns[entry_noun->name] = matching_query_nouns;
    }
    return entry_noun_to_query_nouns;
  }

  // Returns a collection of valid mappings where the key is the query noun, and
  // the value is the entry noun to be populated with the query nouns contents.
  //
  // |remaining_entry_nouns| are all the entry nouns that are yet to be matched.
  // |used_query_nouns| are all the query nouns that have already been used in
  // the current solution.
  std::vector<std::map<std::string, std::string>>
  ComputeResultsFromEntryNounToQueryNounMapping(
      const std::map<std::string, std::vector<std::string>>&
          remaining_entry_nouns,
      const std::set<std::string>& used_query_nouns = {}) {
    std::vector<std::map<std::string, std::string>> result;
    if (remaining_entry_nouns.empty()) {
      return result;
    }

    auto first_entry_noun_it = remaining_entry_nouns.begin();
    const std::string& first_entry_noun_name = first_entry_noun_it->first;
    const std::vector<std::string> query_nouns_for_first_entry =
        first_entry_noun_it->second;

    // If there is only one remaining entry noun, create one result mapping for
    // each viable query noun.
    if (remaining_entry_nouns.size() == 1) {
      for (const auto& query_noun_name : query_nouns_for_first_entry) {
        // Don't create solutions where the query noun has already been used.
        if (used_query_nouns.find(query_noun_name) != used_query_nouns.end()) {
          continue;
        }

        std::map<std::string, std::string> result_map;
        result_map[query_noun_name] = first_entry_noun_name;
        result.push_back(result_map);
      }
      return result;
    }

    for (const auto& query_noun_name : first_entry_noun_it->second) {
      // If the query noun has already been used, it cannot be matched again,
      // and thus the loop continues.
      if (used_query_nouns.find(query_noun_name) != used_query_nouns.end()) {
        continue;
      }

      // The current query noun that will be used by the first entry noun must
      // be added to the used set before computing the solution to the smaller
      // problem.
      std::set<std::string> new_used_query_nouns = used_query_nouns;
      new_used_query_nouns.insert(query_noun_name);

      // Recurse for the remaining nouns.
      std::vector<std::map<std::string, std::string>> solution_for_remainder =
          ComputeResultsFromEntryNounToQueryNounMapping(
              {std::next(remaining_entry_nouns.begin()),
               remaining_entry_nouns.end()},
              new_used_query_nouns);

      // Expand each solution to the smaller problem by inserting the current
      // query noun -> entry noun into the solution.
      for (const auto& existing_solution : solution_for_remainder) {
        std::map<std::string, std::string> updated_solution = existing_solution;
        updated_solution[query_noun_name] = first_entry_noun_name;
        result.push_back(updated_solution);
      }
    }

    return result;
  }

  // Returns true if the any entry of |first_types| is contained in
  // |second_types|.
  bool DoTypesIntersect(const std::vector<std::string>& first_types,
                        const std::vector<std::string>& second_types) {
    const std::set<std::string> first_type_set(first_types.begin(),
                                               first_types.end());
    const std::set<std::string> second_type_set(second_types.begin(),
                                                second_types.end());
    std::set<std::string> intersection;
    std::set_intersection(first_type_set.begin(), first_type_set.end(),
                          second_type_set.begin(), second_type_set.end(),
                          std::inserter(intersection, intersection.begin()));
    return !intersection.empty();
  }

  std::vector<std::string> ToArray(f1dl::VectorPtr<f1dl::StringPtr>& values) {
    std::vector<std::string> ret;
    for (f1dl::StringPtr str : *values) {
      ret.push_back(str.get());
    }
    return ret;
  }

  // Copies the nouns from |query| to the provided resolver result.
  //
  // |noun_mapping| A mapping from the noun names of query to the entry noun
  // that should be populated. If no such mapping exists, the query noun name
  // will be used.
  void CopyNounsToModuleResolverResult(
      const modular::ResolverQueryPtr& query,
      modular::ModuleResolverResult* result,
      std::map<std::string, std::string> noun_mapping = {}) {
    result->create_chain_info = modular::CreateChainInfo::New();
    auto& create_chain_info = result->create_chain_info;  // For convenience.
    for (const auto& query_noun : *query->noun_constraints) {
      const auto& noun = query_noun->constraint;
      std::string name = query_noun->key;
      auto it = noun_mapping.find(query_noun->key);
      if (it != noun_mapping.end()) {
        name = it->second;
      }

      if (noun->is_entity_reference()) {
        auto create_link = modular::CreateLinkInfo::New();
        create_link->initial_data =
            modular::EntityReferenceToJson(noun->get_entity_reference());
        // TODO(thatguy): set |create_link->allowed_types|.
        // TODO(thatguy): set |create_link->permissions|.
        auto property_info = modular::CreateChainPropertyInfo::New();
        property_info->set_create_link(std::move(create_link));
        auto chain_entry = modular::ChainEntry::New();
        chain_entry->key = name;
        chain_entry->value = std::move(property_info);
        create_chain_info->property_info.push_back(std::move(chain_entry));
      } else if (noun->is_link_info()) {
        auto property_info = modular::CreateChainPropertyInfo::New();
        property_info->set_link_path(noun->get_link_info()->path.Clone());
        auto chain_entry = modular::ChainEntry::New();
        chain_entry->key = name;
        chain_entry->value = std::move(property_info);
        create_chain_info->property_info.push_back(std::move(chain_entry));
      } else if (noun->is_json()) {
        auto create_link = modular::CreateLinkInfo::New();
        create_link->initial_data = noun->get_json();
        // TODO(thatguy): set |create_link->allowed_types|.
        // TODO(thatguy): set |create_link->permissions|.
        auto property_info = modular::CreateChainPropertyInfo::New();
        property_info->set_create_link(std::move(create_link));
        auto chain_entry = modular::ChainEntry::New();
        chain_entry->key = name;
        chain_entry->value = std::move(property_info);
        create_chain_info->property_info.push_back(std::move(chain_entry));
      }
      // There's nothing to copy over from 'entity_types', since it only
      // specifies noun constraint information, and no actual content.
    }
  }

  modular::FindModulesResultPtr HandleUrlQuery(
      const modular::ResolverQueryPtr& query) {
    auto mod_result = modular::ModuleResolverResult::New();
    mod_result->module_id = query->url;
    for (const auto& iter : module_resolver_impl_->entries_) {
      if (iter.second->binary == query->url) {
        mod_result->manifest = iter.second.Clone();
      }
    }

    CopyNounsToModuleResolverResult(query, mod_result.get());

    auto result = modular::FindModulesResult::New();
    result->modules.push_back(std::move(mod_result));
    return result;
  }

  modular::FindModulesResultPtr CreateEmptyResult() {
    auto result = modular::FindModulesResult::New();
    result->modules.resize(0);
    return result;
  }

  modular::FindModulesResultPtr result_;

  ModuleResolverImpl* const module_resolver_impl_;
  modular::ResolverQueryPtr query_;
  modular::ResolverScoringInfoPtr scoring_info_;

  // A cache of the Entity types for each noun in |query_|.
  std::map<std::string, std::vector<std::string>> noun_types_cache_;

  std::set<EntryId> candidates_;
  uint32_t num_nouns_countdown_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(FindModulesCall);
};

void ModuleResolverImpl::FindModules(modular::ResolverQueryPtr query,
                                     const FindModulesCallback& done) {
  new FindModulesCall(&operations_, this, std::move(query), nullptr, done);
}

void ModuleResolverImpl::FindModules(
    modular::ResolverQueryPtr query,
    modular::ResolverScoringInfoPtr scoring_info,
    const FindModulesCallback& done) {
  new FindModulesCall(&operations_, this, std::move(query),
                      std::move(scoring_info), done);
}

namespace {
bool StringStartsWith(const std::string& str, const std::string& prefix) {
  return str.compare(0, prefix.length(), prefix) == 0;
}
}  // namespace

void ModuleResolverImpl::OnQuery(UserInputPtr query,
                                 const OnQueryCallback& done) {
  // TODO(thatguy): This implementation is bare-bones. Don't judge.
  // Before adding new member variables to support OnQuery() (and tying the
  // ModuleResolverImpl internals up with what's needed for this method),
  // please split the index-building & querying portion of ModuleResolverImpl
  // out into its own class. Then, make a new class to handle OnQuery() and
  // share the same index instance here and there.

  f1dl::VectorPtr<ProposalPtr> proposals = f1dl::VectorPtr<ProposalPtr>::New(0);
  if (query->text->empty()) {
    auto response = QueryResponse::New();
    response->proposals = std::move(proposals);
    done(std::move(response));
    return;
  }

  for (const auto& id_entry : entries_) {
    const auto& entry = id_entry.second;
    // Simply prefix match on the last element of the verb.
    // Verbs have a convention of being namespaced like java classes:
    // com.google.subdomain.verb
    std::string verb = entry->verb;
    auto parts =
        fxl::SplitString(verb, ".", fxl::kKeepWhitespace, fxl::kSplitWantAll);
    const auto& last_part = parts.back();
    if (StringStartsWith(entry->verb, query->text) ||
        StringStartsWith(last_part.ToString(), query->text)) {
      auto proposal = Proposal::New();
      proposal->id = entry->binary;
      auto create_story = CreateStory::New();
      create_story->module_id = entry->binary;
      auto action = Action::New();
      action->set_create_story(std::move(create_story));
      proposal->on_selected.push_back(std::move(action));

      proposal->display = SuggestionDisplay::New();
      proposal->display->headline =
          std::string("Go go gadget ") + last_part.ToString();
      proposal->display->subheadline = entry->binary;
      proposal->display->color = 0xffffffff;
      proposal->display->annoyance = AnnoyanceType::NONE;

      proposal->confidence = 1.0;  // Yeah, super confident.

      proposals.push_back(std::move(proposal));
    }
  }

  if (proposals->size() > 10) {
    proposals.resize(10);
  }

  auto response = QueryResponse::New();
  response->proposals = std::move(proposals);
  done(std::move(response));
}

void ModuleResolverImpl::OnSourceIdle(const std::string& source_name) {
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

void ModuleResolverImpl::OnNewManifestEntry(
    const std::string& source_name,
    std::string id_in,
    modular::ModuleManifestPtr new_entry) {
  FXL_LOG(INFO) << "New Module manifest " << id_in
                << ": verb = " << new_entry->verb
                << ", binary = " << new_entry->binary;
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
  verb_to_entries_[entry->verb].insert(id);

  for (const auto& constraint : *entry->noun_constraints) {
    for (const auto& type : *constraint->types) {
      noun_type_and_name_to_entries_[std::make_pair(type, constraint->name)]
          .insert(id);
      noun_type_to_entries_[type].insert(id);
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
  verb_to_entries_[entry->verb].erase(id);

  for (const auto& constraint : *entry->noun_constraints) {
    for (const auto& type : *constraint->types) {
      noun_type_and_name_to_entries_[std::make_pair(type, constraint->name)]
          .erase(id);
      noun_type_to_entries_[type].erase(id);
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
