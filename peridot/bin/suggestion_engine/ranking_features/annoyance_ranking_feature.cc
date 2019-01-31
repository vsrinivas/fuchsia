// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/annoyance_ranking_feature.h"

namespace modular {

AnnoyanceRankingFeature::AnnoyanceRankingFeature() = default;

AnnoyanceRankingFeature::~AnnoyanceRankingFeature() = default;

double AnnoyanceRankingFeature::ComputeFeatureInternal(
    const fuchsia::modular::UserInput& query,
    const RankedSuggestion& suggestion) {
  if (suggestion.prototype->proposal.display.annoyance !=
      fuchsia::modular::AnnoyanceType::NONE) {
    return kMaxConfidence;
  }
  return kMinConfidence;
}

}  // namespace modular
