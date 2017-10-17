// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "peridot/bin/suggestion_engine/query_context.h"
#include "peridot/bin/suggestion_engine/ranking_feature.h"
#include "peridot/bin/suggestion_engine/suggestion_channel.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

#include <functional>
#include <vector>

namespace maxwell {
using MatchPredicate =
    std::function<bool(const std::unique_ptr<RankedSuggestion>& suggestion)>;

class RankedSuggestions {
 public:
  RankedSuggestions(SuggestionChannel* channel);

  void AddRankingFeature(double weight,
                         std::shared_ptr<RankingFeature> ranking_feature);
  void Rank(const QueryContext& query_context = {NONE, ""});

  void AddSuggestion(SuggestionPrototype* const prototype);

  void RemoveSuggestion(const std::string& id);
  void RemoveProposal(const std::string& component_url,
                      const std::string& proposal_id);

  void RemoveAllSuggestions();

  RankedSuggestion* GetSuggestion(const std::string& suggestion_id) const;

  RankedSuggestion* GetSuggestion(const std::string& component_url,
                                  const std::string& proposal_id) const;

  const std::vector<std::unique_ptr<RankedSuggestion>>& Get() const {
    return suggestions_;
  }

 private:
  RankedSuggestion* GetMatchingSuggestion(MatchPredicate matchFunction) const;
  void RemoveMatchingSuggestion(MatchPredicate matchFunction);
  void DoStableSort();

  // The channel to push addition/removal events into.
  SuggestionChannel* channel_;

  // The sorted vector of RankedSuggestions, sorted by
  // |ranking_function_|. The vector is re-sorted whenever its
  // contents are modified or when |ranking_function_| is updated.
  // TODO(jwnichols): Should ranking happen automatically or specifically
  // when requested?  I think I would lean toward the latter, since ranking
  // may be expensive.
  std::vector<std::unique_ptr<RankedSuggestion>> suggestions_;

  // Ranking features as a list of (weight, feature) pairs
  std::vector<std::pair<double, std::shared_ptr<RankingFeature>>>
      ranking_features_;

  // The sum of the weights stored in the ranking_features_ vector
  double normalization_factor_;
};

}  // namespace maxwell
