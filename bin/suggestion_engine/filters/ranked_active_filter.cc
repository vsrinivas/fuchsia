// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/filters/ranked_active_filter.h"

#include <list>

namespace modular {

RankedActiveFilter::RankedActiveFilter(
    std::shared_ptr<RankingFeature> ranking_feature)
    : ranking_feature_(ranking_feature) {}

RankedActiveFilter::~RankedActiveFilter() = default;

void RankedActiveFilter::Filter(
    std::vector<std::unique_ptr<RankedSuggestion>>* const suggestions) {
  suggestions->erase(
      std::remove_if(
          suggestions->begin(), suggestions->end(),
          [this](const std::unique_ptr<RankedSuggestion>& ranked_suggestion) {
            double confidence = ranking_feature_->ComputeFeature(
                fuchsia::modular::UserInput(), *ranked_suggestion);
            return confidence >= kMaxConfidence;
          }),
      suggestions->end());
}

}  // namespace modular
