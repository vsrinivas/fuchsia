// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_PROPOSAL_HINT_RANKING_FEATURE_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_PROPOSAL_HINT_RANKING_FEATURE_H_

#include <fuchsia/cpp/modular.h>

#include "peridot/bin/suggestion_engine/ranking_feature.h"

namespace modular {

class ProposalHintRankingFeature : public RankingFeature {
 public:
  ProposalHintRankingFeature();
  ~ProposalHintRankingFeature() override;

 protected:
  double ComputeFeatureInternal(
      const UserInput& query, const RankedSuggestion& suggestion) override;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_RANKING_FEATURES_PROPOSAL_HINT_RANKING_FEATURE_H_
