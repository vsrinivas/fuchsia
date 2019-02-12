// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_RANKED_SUGGESTIONS_LIST_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_RANKED_SUGGESTIONS_LIST_H_

#include <functional>
#include <queue>
#include <vector>

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/suggestion_engine/filters/suggestion_active_filter.h"
#include "peridot/bin/suggestion_engine/filters/suggestion_passive_filter.h"
#include "peridot/bin/suggestion_engine/rankers/ranker.h"
#include "peridot/bin/suggestion_engine/ranking_features/ranking_feature.h"
#include "peridot/bin/suggestion_engine/suggestion_prototype.h"

namespace modular {
using MatchPredicate =
    fit::function<bool(const std::unique_ptr<RankedSuggestion>& suggestion)>;

// Stores a list of RankedSuggestion objects and the features by which they
// should be ranked.  Ranking must be explicitly triggered via the Rank
// method.
class RankedSuggestionsList {
 public:
  RankedSuggestionsList();
  ~RankedSuggestionsList();

  void SetRanker(std::unique_ptr<Ranker> ranker);

  void SetActiveFilters(
      std::vector<std::unique_ptr<SuggestionActiveFilter>>&& active_filters);

  void SetPassiveFilters(
      std::vector<std::unique_ptr<SuggestionPassiveFilter>>&& passive_filters);

  void AddSuggestion(SuggestionPrototype* const prototype);

  void AddSuggestion(std::unique_ptr<RankedSuggestion> ranked_suggestion);

  // Returns |true| if and only if the suggestion was present and is removed.
  bool RemoveProposal(const std::string& component_url,
                      const std::string& proposal_id);

  void RemoveAllSuggestions();

  RankedSuggestion* GetSuggestion(const std::string& suggestion_id) const;

  RankedSuggestion* GetSuggestion(const std::string& component_url,
                                  const std::string& proposal_id) const;

  const std::vector<std::unique_ptr<RankedSuggestion>>& Get() const {
    return suggestions_;
  }

  void Refresh(
      const fuchsia::modular::UserInput& query = fuchsia::modular::UserInput());

 private:
  RankedSuggestion* GetMatchingSuggestion(MatchPredicate matchFunction) const;
  bool RemoveMatchingSuggestion(MatchPredicate matchFunction);
  void DoStableSort();
  void Rank(
      const fuchsia::modular::UserInput& query = fuchsia::modular::UserInput());

  // The sorted vector of RankedSuggestions, sorted by
  // |ranking_function_|. The vector is re-sorted whenever its
  // contents are modified or when |ranking_function_| is updated.
  // TODO(jwnichols): Should ranking happen automatically or specifically
  // when requested?  I think I would lean toward the latter, since ranking
  // may be expensive.
  std::vector<std::unique_ptr<RankedSuggestion>> suggestions_;

  std::unique_ptr<Ranker> ranker_;

  // The fuchsia::modular::Suggestion Filters associated with this List of
  // Ranked Suggestions
  std::vector<std::unique_ptr<SuggestionActiveFilter>>
      suggestion_active_filters_;
  std::vector<std::unique_ptr<SuggestionPassiveFilter>>
      suggestion_passive_filters_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_RANKED_SUGGESTIONS_LIST_H_
