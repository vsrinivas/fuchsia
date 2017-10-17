// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranked_suggestions.h"

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

RankedSuggestions::RankedSuggestions(SuggestionChannel* channel)
    : channel_(channel), normalization_factor_(0.0) {}

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

void RankedSuggestions::AddRankingFeature(
    double weight,
    std::shared_ptr<RankingFeature> ranking_feature) {
  ranking_features_.emplace_back(weight, ranking_feature);
  // only incorporate positive weights into the normalization factor
  if (weight > 0.0)
    normalization_factor_ += weight;
}

void RankedSuggestions::Rank(const QueryContext& query_context) {
  for (auto& suggestion : suggestions_) {
    double confidence = 0.0;
    for (auto& feature : ranking_features_) {
      confidence += feature.first *
                    feature.second->ComputeFeature(query_context, *suggestion);
    }
    // TODO(jwnichols): Reconsider this normalization approach.
    // Weights may be negative, so there is some chance that the calculated
    // confidence score will be negative.  We pull the calculated score up to
    // zero to guarantee final confidence values stay within the 0-1 range.
    FXL_CHECK(normalization_factor_ > 0.0);
    suggestion->confidence = std::max(confidence, 0.0) / normalization_factor_;
  }
  DoStableSort();
  channel_->DispatchInvalidate();
}

void RankedSuggestions::AddSuggestion(SuggestionPrototype* prototype) {
  std::unique_ptr<RankedSuggestion> ranked_suggestion =
      std::make_unique<RankedSuggestion>();
  ranked_suggestion->prototype = prototype;
  suggestions_.push_back(std::move(ranked_suggestion));
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
                     return a->confidence > b->confidence;
                   });
}

// End of private sorting methods.

}  // namespace maxwell
