// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/kronk_ranking_feature.h"

namespace maxwell {

KronkRankingFeature::KronkRankingFeature() = default;

KronkRankingFeature::~KronkRankingFeature() = default;

double KronkRankingFeature::ComputeFeatureInternal(
    const QueryContext& query_context,
    const RankedSuggestion& suggestion) {
  if (suggestion.prototype->source_url.find("kronk") != std::string::npos) {
    return kMaxConfidence;
  } else {
    return kMinConfidence;
  }
}

}  // namespace maxwell
