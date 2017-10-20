// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "peridot/bin/suggestion_engine/ranking_feature.h"

namespace maxwell {

class ProposalHintRankingFeature : public RankingFeature {
 public:
  ProposalHintRankingFeature();
  ~ProposalHintRankingFeature() override;

 protected:
  double ComputeFeatureInternal(const QueryContext& query_context,
                                const RankedSuggestion& suggestion) override;
};

}  // namespace maxwell
