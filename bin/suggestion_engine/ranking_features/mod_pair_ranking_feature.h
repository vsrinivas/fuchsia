// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_MOD_PAIR_RANKING_FEATURE_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_MOD_PAIR_RANKING_FEATURE_H_

#include <unordered_map>

#include <fuchsia/cpp/modular.h>

#include "peridot/bin/suggestion_engine/ranking_feature.h"

namespace modular {

class ModPairRankingFeature : public RankingFeature {
 public:
  ModPairRankingFeature(bool init_data = true);
  ~ModPairRankingFeature() override;

  void LoadDataFromFile(const std::string& filepath);

 private:
  double ComputeFeatureInternal(
      const UserInput& query, const RankedSuggestion& suggestion) override;

  ContextSelectorPtr CreateContextSelectorInternal() override;

  std::unordered_map<std::string, std::unordered_map<std::string, double>>
      module_pairs_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_MOD_PAIR_RANKING_FEATURE_H_
