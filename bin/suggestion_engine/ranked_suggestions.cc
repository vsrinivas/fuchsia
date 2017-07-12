// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/ranked_suggestions.h"

#include <algorithm>
#include <string>

namespace maxwell {

RankedSuggestion* RankedSuggestions::GetMatchingSuggestion(
    std::function<bool(RankedSuggestion* suggestion)> matchFunction) const {
  auto findIter =
      std::find_if(suggestions_.begin(), suggestions_.end(), matchFunction);
  if (findIter != suggestions_.end())
    return *findIter;
  return nullptr;
}

void RankedSuggestions::RemoveMatchingSuggestion(
    std::function<bool(RankedSuggestion* suggestion)> matchFunction) {
  auto removeIter =
      std::remove_if(suggestions_.begin(), suggestions_.end(), matchFunction);
  suggestions_.erase(removeIter, suggestions_.end());
  channel_->DispatchInvalidate();
}

void RankedSuggestions::UpdateRankingFunction(
    RankingFunction ranking_function) {
  ranking_function_ = ranking_function;
  std::vector<RankedSuggestion*>::const_iterator it;
  for (it = suggestions_.begin(); it != suggestions_.end(); it++) {
    (*it)->rank = ranking_function((*it)->prototype);
  }
  DoStableSort();
}

void RankedSuggestions::AddSuggestion(
    const SuggestionPrototype* const prototype) {
  const int64_t rank = ranking_function_(prototype);

  RankedSuggestion* existing_suggestion =
      GetMatchingSuggestion([&prototype](RankedSuggestion* suggestion) {
        return suggestion->prototype->proposal->id == prototype->proposal->id;
      });
  if (existing_suggestion && (existing_suggestion->rank != rank))
    RemoveSuggestion(existing_suggestion->prototype->suggestion_id);

  RankedSuggestion* ranked_suggestion = new RankedSuggestion();
  ranked_suggestion->rank = rank;
  ranked_suggestion->prototype = prototype;
  suggestions_.push_back(ranked_suggestion);
  DoStableSort();
  channel_->DispatchInvalidate();
}

void RankedSuggestions::RemoveProposal(const std::string& component_url,
                                       const std::string& proposal_id) {
  RemoveMatchingSuggestion(
      [component_url, proposal_id](RankedSuggestion* suggestion) {
        return (suggestion->prototype->proposal->id == proposal_id) &&
               (suggestion->prototype->source_url == component_url);
      });
}

void RankedSuggestions::RemoveSuggestion(const std::string& suggestion_id) {
  RemoveMatchingSuggestion([suggestion_id](RankedSuggestion* suggestion) {
    return suggestion->prototype->suggestion_id == suggestion_id;
  });
}

RankedSuggestion* RankedSuggestions::GetSuggestion(
    const std::string& suggestion_id) const {
  return GetMatchingSuggestion([suggestion_id](RankedSuggestion* suggestion) {
    return suggestion->prototype->suggestion_id == suggestion_id;
  });
}

void RankedSuggestions::RemoveAllSuggestions() {
  std::vector<RankedSuggestion*>::const_iterator it;
  for (it = suggestions_.begin(); it != suggestions_.end(); it++) {
    channel_->DispatchOnRemoveSuggestion(*it);
  }
  suggestions_.clear();
}

// Start of private sorting methods.

void RankedSuggestions::DoStableSort() {
  std::stable_sort(suggestions_.begin(), suggestions_.end(),
                   [](RankedSuggestion* a, RankedSuggestion* b) {
                     return a->rank < b->rank;
                   });
}

// End of private sorting methods.

}  // namespace maxwell
