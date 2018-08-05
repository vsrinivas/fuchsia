// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/filters/ranked_passive_filter.h"

#include <list>

namespace modular {

RankedPassiveFilter::RankedPassiveFilter(
    std::shared_ptr<RankingFeature> ranking_feature)
    : ranking_feature_(ranking_feature) {}

RankedPassiveFilter::~RankedPassiveFilter() = default;

// If the confidence of the ranking feature is 1.0 then this filter returns
// true.
bool RankedPassiveFilter::Filter(
    const std::unique_ptr<RankedSuggestion>& ranked_suggestion) {
  double confidence = ranking_feature_->ComputeFeature(
      fuchsia::modular::UserInput(), *ranked_suggestion);
  return confidence == kMaxConfidence;
}

}  // namespace modular
