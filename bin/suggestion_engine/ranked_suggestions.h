// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "peridot/bin/suggestion_engine/ranking.h"
#include "peridot/bin/suggestion_engine/suggestion_channel.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

#include <functional>
#include <vector>

namespace maxwell {
using MatchPredicate =
    std::function<bool(const std::unique_ptr<RankedSuggestion>& suggestion)>;

class RankedSuggestions {
 public:
  RankedSuggestions(SuggestionChannel* channel) : channel_(channel) {}

  void UpdateRankingFunction(RankingFunction ranking_function);

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
  // The function that will rank all SuggestionPrototypes.
  RankingFunction ranking_function_;

  // The channel to push addition/removal events into.
  SuggestionChannel* channel_;

  // The sorted vector of RankedSuggestions, sorted by
  // |ranking_function_|. The vector is re-sorted whenever its
  // contents are modified or when |ranking_function_| is updated.
  std::vector<std::unique_ptr<RankedSuggestion>> suggestions_;
};

}  // namespace maxwell
