// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/query_match_ranking_feature.h"

namespace maxwell {

QueryMatchRankingFeature::QueryMatchRankingFeature() = default;

QueryMatchRankingFeature::~QueryMatchRankingFeature() = default;

double QueryMatchRankingFeature::ComputeFeatureInternal(
    const UserInput& query,
    const RankedSuggestion& suggestion) {
  std::string text = suggestion.prototype->proposal->display->headline;
  std::string norm_query = query.text;

  std::transform(text.begin(), text.end(), text.begin(), ::tolower);
  std::transform(norm_query.begin(), norm_query.end(), norm_query.begin(),
                 ::tolower);

  // TODO(jwnichols): replace with a score based on Longest Common Substring
  auto pos = text.find(norm_query);
  if (pos == std::string::npos)
    return kMinConfidence;

  return static_cast<double>(norm_query.size()) /
         static_cast<double>(text.size());
}

}  // namespace maxwell
