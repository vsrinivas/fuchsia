// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranked_suggestions_list.h"

#include <algorithm>
#include <string>

#include <lib/context/cpp/context_helper.h>
#include <lib/fxl/logging.h>

namespace modular {

MatchPredicate GetSuggestionMatcher(const std::string& component_url,
                                    const std::string& proposal_id) {
  return [component_url,
          proposal_id](const std::unique_ptr<RankedSuggestion>& suggestion) {
    return (suggestion->prototype->proposal.id == proposal_id) &&
           (suggestion->prototype->source_url == component_url);
  };
}

MatchPredicate GetSuggestionMatcher(const std::string& suggestion_id) {
  return [suggestion_id](const std::unique_ptr<RankedSuggestion>& suggestion) {
    return suggestion->prototype->suggestion_id == suggestion_id;
  };
}

RankedSuggestionsList::RankedSuggestionsList() {}

RankedSuggestionsList::~RankedSuggestionsList() = default;

void RankedSuggestionsList::SetActiveFilters(
    std::vector<std::unique_ptr<SuggestionActiveFilter>>&& active_filters) {
  suggestion_active_filters_ = std::move(active_filters);
}

void RankedSuggestionsList::SetPassiveFilters(
    std::vector<std::unique_ptr<SuggestionPassiveFilter>>&& passive_filters) {
  suggestion_passive_filters_ = std::move(passive_filters);
}

void RankedSuggestionsList::SetRanker(std::unique_ptr<Ranker> ranker) {
  ranker_ = std::move(ranker);
}

RankedSuggestion* RankedSuggestionsList::GetMatchingSuggestion(
    MatchPredicate matchFunction) const {
  auto findIter =
      std::find_if(suggestions_.begin(), suggestions_.end(), matchFunction);
  if (findIter != suggestions_.end()) {
    return findIter->get();
  }
  return nullptr;
}

bool RankedSuggestionsList::RemoveMatchingSuggestion(
    MatchPredicate matchFunction) {
  auto remove_iter =
      std::remove_if(suggestions_.begin(), suggestions_.end(), matchFunction);
  if (remove_iter == suggestions_.end()) {
    return false;
  } else {
    suggestions_.erase(remove_iter, suggestions_.end());
    return true;
  }
}

void RankedSuggestionsList::Rank(const fuchsia::modular::UserInput& query) {
  if (!ranker_) {
    FXL_LOG(WARNING)
        << "RankedSuggestionList.Rank ignored since no ranker was set.";
    return;
  }
  for (auto& suggestion : suggestions_) {
    suggestion->confidence = ranker_->Rank(query, *suggestion);
    FXL_VLOG(1) << "fuchsia::modular::Proposal "
                << suggestion->prototype->proposal.display.headline
                << " confidence " << suggestion->prototype->proposal.confidence
                << " => " << suggestion->confidence;
  }
  DoStableSort();
}

void RankedSuggestionsList::AddSuggestion(SuggestionPrototype* prototype) {
  suggestions_.push_back(RankedSuggestion::New(prototype));
}

void RankedSuggestionsList::AddSuggestion(
    std::unique_ptr<RankedSuggestion> ranked_suggestion) {
  suggestions_.push_back(std::move(ranked_suggestion));
}

bool RankedSuggestionsList::RemoveProposal(const std::string& component_url,
                                           const std::string& proposal_id) {
  return RemoveMatchingSuggestion(
      GetSuggestionMatcher(component_url, proposal_id));
}

RankedSuggestion* RankedSuggestionsList::GetSuggestion(
    const std::string& suggestion_id) const {
  return GetMatchingSuggestion(GetSuggestionMatcher(suggestion_id));
}

RankedSuggestion* RankedSuggestionsList::GetSuggestion(
    const std::string& component_url, const std::string& proposal_id) const {
  return GetMatchingSuggestion(
      GetSuggestionMatcher(component_url, proposal_id));
}

void RankedSuggestionsList::RemoveAllSuggestions() { suggestions_.clear(); }

void RankedSuggestionsList::Refresh(const fuchsia::modular::UserInput& query) {
  // Apply the active filters that modify the entire suggestions list.
  // TODO(miguelfrde): Fix. Currently not WAI. For dead stories for example,
  // this will remove suggestions that belong to a story that is being created.
  for (const auto& active_filter : suggestion_active_filters_) {
    active_filter->Filter(&suggestions_);
  }

  // Apply the passive filters that hide some of the suggestions.
  for (auto& suggestion : suggestions_) {
    suggestion->hidden = std::any_of(
        suggestion_passive_filters_.begin(), suggestion_passive_filters_.end(),
        [&suggestion](const std::unique_ptr<SuggestionPassiveFilter>& f) {
          return f->Filter(suggestion);
        });
  }

  // Rerank and sort the updated suggestions_ list
  Rank(query);
}

// Start of private sorting methods.

void RankedSuggestionsList::DoStableSort() {
  std::stable_sort(suggestions_.begin(), suggestions_.end(),
                   [](const std::unique_ptr<RankedSuggestion>& a,
                      const std::unique_ptr<RankedSuggestion>& b) {
                     return a->confidence > b->confidence;
                   });
}

// End of private sorting methods.

}  // namespace modular
