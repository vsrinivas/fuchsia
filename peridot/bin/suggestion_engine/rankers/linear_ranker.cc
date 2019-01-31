// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/rankers/linear_ranker.h"

#include <lib/fxl/logging.h>

namespace modular {

LinearRanker::LinearRanker() : normalization_factor_(0.0) {}

LinearRanker::~LinearRanker() = default;

void LinearRanker::AddRankingFeature(
    double weight, std::shared_ptr<RankingFeature> ranking_feature) {
  ranking_features_.emplace_back(weight, ranking_feature);
  // Only incorporate positive weights into the normalization factor.
  if (weight > 0.0) {
    normalization_factor_ += weight;
  }
}

double LinearRanker::Rank(const fuchsia::modular::UserInput& query,
                          const RankedSuggestion& suggestion) {
  double confidence = 0.0;
  for (auto& feature : ranking_features_) {
    confidence +=
        feature.first * feature.second->ComputeFeature(query, suggestion);
  }
  // TODO(jwnichols): Reconsider this normalization approach.
  // Weights may be negative, so there is some chance that the calculated
  // confidence score will be negative.  We pull the calculated score up to
  // zero to guarantee final confidence values stay within the 0-1 range.
  FXL_CHECK(normalization_factor_ > 0.0);
  return std::max(confidence, 0.0) / normalization_factor_;
}

}  // namespace modular
