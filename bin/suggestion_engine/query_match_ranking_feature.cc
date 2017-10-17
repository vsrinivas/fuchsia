// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/query_match_ranking_feature.h"

namespace maxwell {

QueryMatchRankingFeature::QueryMatchRankingFeature() = default;

QueryMatchRankingFeature::~QueryMatchRankingFeature() = default;

double QueryMatchRankingFeature::ComputeFeatureInternal(
    const QueryContext& query_context,
    const RankedSuggestion& suggestion) {
  if (query_context.type == QueryType::TEXT ||
      query_context.type == QueryType::SPEECH) {
    std::string text = suggestion.prototype->proposal->display->headline;
    std::string query = query_context.query;

    std::transform(text.begin(), text.end(), text.begin(), ::tolower);
    std::transform(query.begin(), query.end(), query.begin(), ::tolower);

    // TODO(jwnichols): replace with a score based on Longest Common Substring
    auto pos = text.find(query);
    if (pos == std::string::npos)
      return kMinConfidence;

    return static_cast<double>(query.size()) / static_cast<double>(text.size());
  } else {
    return kMinConfidence;
  }
}

}  // namespace maxwell
