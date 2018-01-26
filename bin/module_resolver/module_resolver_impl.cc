// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "peridot/bin/module_resolver/module_resolver_impl.h"

#include "garnet/public/lib/fxl/strings/split_string.h"
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

void ModuleResolverImpl::BindQueryHandler(
    fidl::InterfaceRequest<QueryHandler> request) {
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

  // Given a verb, we:
  // 1) Find all modules that can handle the verb in this query.
  // 2) Find all modules that can handle any of the (noun,type)s in this query.
  //    Note that this includes modules that only satisfy a subset of the query
  //    input.
  // 3) Intersect 1) and 2) to find modules that satisfy the query.
  void Run() {
    FlowToken flow{this, &result_};

    if (query_->url) {
      // Client already knows what Module they want to use, so we'll
      // short-circuit resolution.
      result_ = HandleUrlQuery(query_);
      return;
    }

    if (!query_->verb) {
      // TODO(thatguy): Add no-verb resolution.
      result_ = CreateEmptyResult();
      return;
    }

    auto verb_it = module_resolver_impl_->verb_to_entries_.find(query_->verb);
    if (verb_it == module_resolver_impl_->verb_to_entries_.end()) {
      result_ = CreateEmptyResult();
      return;
    }

    candidates_ = verb_it->second;

    // For each noun in the ResolverQuery, try to find Modules that provide the
    // types in the noun as constraints.
    if (query_->noun_constraints.is_null() ||
        query_->noun_constraints.size() == 0) {
      Finally(flow);
      return;
    }

    num_nouns_countdown_ = query_->noun_constraints.size();
    for (const auto& noun_entry : query_->noun_constraints) {
      const auto& noun_name = noun_entry.GetKey();
      const auto& noun_constraints = noun_entry.GetValue();

      module_resolver_impl_->type_helper_.GetNounTypes(
          noun_constraints,
          [noun_name, flow, this](std::vector<std::string> types) {
            ProcessNounTypes(noun_name, std::move(types));
            if (--num_nouns_countdown_ == 0) {
              Finally(flow);
            }
          });
    }
  }

 private:
  // |noun_name| and |types| come from the ResolverQuery.
  void ProcessNounTypes(const std::string& noun_name,
                        std::vector<std::string> types) {
    noun_types_cache_[noun_name] = types;

    // The types list we have is an OR - any Module that can handle any of the
    // types for this noun is valid, so we union all valid resolutions. First,
    // we gather all such modules, regardless of if they handle the verb.
    std::set<EntryId> noun_type_entries;
    for (const auto& type : types) {
      auto noun_it = module_resolver_impl_->noun_type_to_entries_.find(
          std::make_pair(type, noun_name));
      if (noun_it == module_resolver_impl_->noun_type_to_entries_.end())
        continue;

      noun_type_entries.insert(noun_it->second.begin(), noun_it->second.end());
    }

    // The target Module must match the types in every noun specified in the
    // ResolverQuery, so here we do a set intersection with our possible set of
    // candidates.
    std::set<EntryId> new_result_entries;
    std::set_intersection(
        candidates_.begin(), candidates_.end(), noun_type_entries.begin(),
        noun_type_entries.end(),
        std::inserter(new_result_entries, new_result_entries.begin()));
    candidates_.swap(new_result_entries);
  }

  void Finally(FlowToken flow) {
    if (candidates_.empty()) {
      result_ = CreateEmptyResult();
      return;
    }

    result_ = modular::FindModulesResult::New();
    for (auto id : candidates_) {
      auto entry_it = module_resolver_impl_->entries_.find(id);
      FXL_CHECK(entry_it != module_resolver_impl_->entries_.end()) << id;
      const auto& entry = entry_it->second;

      auto result = modular::ModuleResolverResult::New();
      result->module_id = entry.binary;
      result->local_name = entry.local_name;
      CopyNounsToModuleResolverResult(query_, &result);

      result_->modules.push_back(std::move(result));
    }
  }

  void CopyNounsToModuleResolverResult(
      const modular::ResolverQueryPtr& query,
      modular::ModuleResolverResultPtr* result) {
    (*result)->initial_nouns.mark_non_null();
    (*result)->create_chain_info = modular::CreateChainInfo::New();
    auto& create_chain_info = (*result)->create_chain_info;  // For convenience.
    create_chain_info->property_info.mark_non_null();
    for (auto entry : query->noun_constraints) {
      const auto& name = entry.GetKey();
      const auto& noun = entry.GetValue();

      if (noun->is_entity_reference()) {
        // TODO(thatguy): Remove this once no more Modules are using the root
        // Link. MI4-736
        (*result)->initial_nouns[name] =
            modular::EntityReferenceToJson(noun->get_entity_reference());

        auto create_link = modular::CreateLinkInfo::New();
        create_link->initial_data =
            modular::EntityReferenceToJson(noun->get_entity_reference());
        // TODO(thatguy): set |create_link->allowed_types|.
        // TODO(thatguy): set |create_link->permissions|.
        auto property_info = modular::CreateChainPropertyInfo::New();
        property_info->set_create_link(std::move(create_link));
        create_chain_info->property_info[name] = std::move(property_info);
      } else if (noun->is_link_info()) {
        auto property_info = modular::CreateChainPropertyInfo::New();
        property_info->set_link_path(noun->get_link_info()->path.Clone());
        create_chain_info->property_info[name] = std::move(property_info);
      } else if (noun->is_json()) {
        // TODO(thatguy): Remove this once no more Modules are using the root
        // Link. MI4-736
        (*result)->initial_nouns[name] = noun->get_json();

        auto create_link = modular::CreateLinkInfo::New();
        create_link->initial_data = noun->get_json();
        // TODO(thatguy): set |create_link->allowed_types|.
        // TODO(thatguy): set |create_link->permissions|.
        auto property_info = modular::CreateChainPropertyInfo::New();
        property_info->set_create_link(std::move(create_link));
        create_chain_info->property_info[name] = std::move(property_info);
      }
      // There's nothing to copy over from 'entity_types', since it only
      // specifies noun constraint information, and no actual content.
    }
  }

  modular::FindModulesResultPtr HandleUrlQuery(
      const modular::ResolverQueryPtr& query) {
    auto result = modular::FindModulesResult::New();

    result->modules.resize(1);

    auto mod_result = modular::ModuleResolverResult::New();
    mod_result->module_id = query->url;
    mod_result->local_name = "n/a";

    CopyNounsToModuleResolverResult(query, &mod_result);

    result->modules[0] = std::move(mod_result);
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

  fidl::Array<ProposalPtr> proposals = fidl::Array<ProposalPtr>::New(0);
  if (query->text.empty()) {
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
    auto parts = fxl::SplitString(entry.verb, ".", fxl::kKeepWhitespace,
                                  fxl::kSplitWantAll);
    const auto& last_part = parts.back();
    if (StringStartsWith(entry.verb, query->text) ||
        StringStartsWith(last_part.ToString(), query->text)) {
      auto proposal = Proposal::New();
      proposal->id = entry.binary;
      auto create_story = CreateStory::New();
      create_story->module_id = entry.binary;
      auto action = Action::New();
      action->set_create_story(std::move(create_story));
      proposal->on_selected.push_back(std::move(action));

      proposal->display = SuggestionDisplay::New();
      proposal->display->headline =
          std::string("Go go gadget ") + last_part.ToString();
      proposal->display->subheadline = entry.binary;
      proposal->display->color = 0xffffffff;
      proposal->display->annoyance = AnnoyanceType::NONE;

      proposal->confidence = 1.0;  // Yeah, super confident.

      proposals.push_back(std::move(proposal));
    }
  }

  if (proposals.size() > 10) {
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
    modular::ModuleManifestSource::Entry new_entry) {
  FXL_LOG(INFO) << "New Module manifest " << id_in
                << ": verb = " << new_entry.verb
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
  verb_to_entries_[entry.verb].insert(id);

  for (const auto& constraint : entry.noun_constraints) {
    for (const auto& type : constraint.types) {
      noun_type_to_entries_[std::make_pair(type, constraint.name)].insert(id);
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
  verb_to_entries_[entry.verb].erase(id);

  for (const auto& constraint : entry.noun_constraints) {
    for (const auto& type : constraint.types) {
      noun_type_to_entries_[std::make_pair(type, constraint.name)].erase(id);
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
