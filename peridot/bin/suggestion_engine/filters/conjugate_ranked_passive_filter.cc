// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/filters/conjugate_ranked_passive_filter.h"

#include <list>

namespace modular {

ConjugateRankedPassiveFilter::ConjugateRankedPassiveFilter(
    std::shared_ptr<RankingFeature> ranking_feature)
    : ranking_feature_(ranking_feature) {}

ConjugateRankedPassiveFilter::~ConjugateRankedPassiveFilter() = default;

// If the confidence of the ranking feature is 0.0 then this filter returns
// true.
// Example usage with FocusedStoryRankingFeature. It should hide suggestions
// with story affinity true that are not focused:
//   - StoryAffinity=false, Focused=...   => 1.0 => false
//   - StoryAffinity=true,  Focused=true  => 1.0 => false
//   - StoryAffinity=true,  Focused=false => 0.0 => true
bool ConjugateRankedPassiveFilter::Filter(
    const std::unique_ptr<RankedSuggestion>& ranked_suggestion) {
  double confidence = ranking_feature_->ComputeFeature(
      fuchsia::modular::UserInput(), *ranked_suggestion);
  return confidence == kMinConfidence;
}

}  // namespace modular
