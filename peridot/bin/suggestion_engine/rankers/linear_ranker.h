// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_RANKERS_LINEAR_RANKER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_RANKERS_LINEAR_RANKER_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/suggestion_engine/ranked_suggestion.h"
#include "peridot/bin/suggestion_engine/rankers/ranker.h"
#include "peridot/bin/suggestion_engine/ranking_features/ranking_feature.h"

namespace modular {

// Ranks based on a linear combination using the set ranking features and its
// weights.
// Rank(q, x) =
class LinearRanker : public Ranker {
 public:
  LinearRanker();
  ~LinearRanker() override;

  // Computes the new confidence of the suggestion.
  // Rank(q, s) = w_1*f_1(q, s) + w_2*f_2(q, s) + ... + w_i*f_i(q, s) where
  // f_i is a ranking feature and w_i is its weight provided through
  // AddRankingFeature.
  // The data from |query| and |suggestion| needed depends on the data each
  // ranking feature needs.
  double Rank(const fuchsia::modular::UserInput& query,
              const RankedSuggestion& suggestion) override;

  // Sets a ranking feature and associates it to the given weight for the linear
  // combination.
  void AddRankingFeature(double weight,
                         std::shared_ptr<RankingFeature> ranking_feature);

 private:
  // Ranking features as a list of (weight, feature) pairs
  std::vector<std::pair<double, std::shared_ptr<RankingFeature>>>
      ranking_features_;

  // The sum of the weights stored in the ranking_features_ vector
  double normalization_factor_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_RANKERS_LINEAR_RANKER_H_
