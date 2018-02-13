// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_MOD_PAIR_RANKING_FEATURE_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_MOD_PAIR_RANKING_FEATURE_H_

#include <unordered_map>

#include "lib/context/fidl/context_reader.fidl.h"

#include "peridot/bin/suggestion_engine/ranking_feature.h"

namespace maxwell {

class ModPairRankingFeature : public RankingFeature {
 public:
  ModPairRankingFeature();
  ~ModPairRankingFeature() override;


 protected:
  double ComputeFeatureInternal(
      const UserInput& query, const RankedSuggestion& suggestion,
      const f1dl::Array<ContextValuePtr>& context_update_values) override;

  ContextSelectorPtr CreateContextSelectorInternal() override;

 private:
  void InitPairProbabilitiesMap();

  std::unordered_map<std::string, std::unordered_map<std::string, double>>
      probabilities_;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_MOD_PAIR_RANKING_FEATURE_H_
