// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_KRONK_RANKING_FEATURE_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_KRONK_RANKING_FEATURE_H_

#include "peridot/bin/suggestion_engine/ranking_feature.h"

namespace maxwell {

class KronkRankingFeature : public RankingFeature {
 public:
  KronkRankingFeature();
  ~KronkRankingFeature() override;

 protected:
  double ComputeFeatureInternal(const UserInput& query,
                                const RankedSuggestion& suggestion) override;
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_KRONK_RANKING_FEATURE_H_
