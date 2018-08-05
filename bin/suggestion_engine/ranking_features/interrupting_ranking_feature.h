// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_INTERRUPTING_RANKING_FEATURE_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_INTERRUPTING_RANKING_FEATURE_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/suggestion_engine/ranking_features/ranking_feature.h"

namespace modular {

class InterruptingRankingFeature : public RankingFeature {
 public:
  InterruptingRankingFeature();
  ~InterruptingRankingFeature() override;

 private:
  double ComputeFeatureInternal(const fuchsia::modular::UserInput& query,
                                const RankedSuggestion& suggestion) override;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_INTERRUPTING_RANKING_FEATURE_H_
