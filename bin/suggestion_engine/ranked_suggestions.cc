// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/ranked_suggestions.h"

#include <algorithm>
#include <string>

namespace maxwell {

MatchPredicate GetSuggestionMatcher(const std::string& component_url,
                                    const std::string& proposal_id) {
  return [component_url,
          proposal_id](const std::unique_ptr<RankedSuggestion>& suggestion) {
    return (suggestion->prototype->proposal->id == proposal_id) &&
           (suggestion->prototype->source_url == component_url);
  };
}

MatchPredicate GetSuggestionMatcher(const std::string& suggestion_id) {
  return [suggestion_id](const std::unique_ptr<RankedSuggestion>& suggestion) {
    return suggestion->prototype->suggestion_id == suggestion_id;
  };
}

RankedSuggestion* RankedSuggestions::GetMatchingSuggestion(
    MatchPredicate matchFunction) const {
  auto findIter =
      std::find_if(suggestions_.begin(), suggestions_.end(), matchFunction);
  if (findIter != suggestions_.end())
    return findIter->get();
  return nullptr;
}

void RankedSuggestions::RemoveMatchingSuggestion(MatchPredicate matchFunction) {
  auto removeIter =
      std::remove_if(suggestions_.begin(), suggestions_.end(), matchFunction);
  suggestions_.erase(removeIter, suggestions_.end());
  channel_->DispatchInvalidate();
}

void RankedSuggestions::UpdateRankingFunction(
    RankingFunction ranking_function) {
  ranking_function_ = ranking_function;
  for (auto& suggestion : suggestions_) {
    suggestion->rank = ranking_function(suggestion->prototype);
  }
  DoStableSort();
}

void RankedSuggestions::AddSuggestion(SuggestionPrototype* prototype) {
  const int64_t rank = ranking_function_(prototype);
  std::unique_ptr<RankedSuggestion> ranked_suggestion =
      std::make_unique<RankedSuggestion>();
  ranked_suggestion->rank = rank;
  ranked_suggestion->prototype = prototype;
  suggestions_.push_back(std::move(ranked_suggestion));
  DoStableSort();
  channel_->DispatchInvalidate();
}

void RankedSuggestions::RemoveProposal(const std::string& component_url,
                                       const std::string& proposal_id) {
  RemoveMatchingSuggestion(GetSuggestionMatcher(component_url, proposal_id));
}

void RankedSuggestions::RemoveSuggestion(const std::string& suggestion_id) {
  RemoveMatchingSuggestion(GetSuggestionMatcher(suggestion_id));
}

RankedSuggestion* RankedSuggestions::GetSuggestion(
    const std::string& suggestion_id) const {
  return GetMatchingSuggestion(GetSuggestionMatcher(suggestion_id));
}

RankedSuggestion* RankedSuggestions::GetSuggestion(
    const std::string& component_url,
    const std::string& proposal_id) const {
  return GetMatchingSuggestion(
      GetSuggestionMatcher(component_url, proposal_id));
}

void RankedSuggestions::RemoveAllSuggestions() {
  suggestions_.clear();
  channel_->DispatchInvalidate();
}

// Start of private sorting methods.

void RankedSuggestions::DoStableSort() {
  std::stable_sort(suggestions_.begin(), suggestions_.end(),
                   [](const std::unique_ptr<RankedSuggestion>& a,
                      const std::unique_ptr<RankedSuggestion>& b) {
                     return a->rank < b->rank;
                   });
}

// End of private sorting methods.

}  // namespace maxwell
