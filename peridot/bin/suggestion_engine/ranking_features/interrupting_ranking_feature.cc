// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/interrupting_ranking_feature.h"

namespace modular {

InterruptingRankingFeature::InterruptingRankingFeature() {}

InterruptingRankingFeature::~InterruptingRankingFeature() = default;

double InterruptingRankingFeature::ComputeFeatureInternal(
    const fuchsia::modular::UserInput& query,
    const RankedSuggestion& ranked_suggestion) {
  if (ranked_suggestion.interrupting) {
    return kMaxConfidence;
  }
  return kMinConfidence;
}

}  // namespace modular
