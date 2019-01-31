// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/decision_policies/rank_over_threshold_decision_policy.h"

namespace modular {

RankOverThresholdDecisionPolicy::RankOverThresholdDecisionPolicy(
    std::unique_ptr<Ranker> ranker, double threshold)
    : ranker_(std::move(ranker)), threshold_(threshold) {}

RankOverThresholdDecisionPolicy::~RankOverThresholdDecisionPolicy() = default;

bool RankOverThresholdDecisionPolicy::Accept(
    const RankedSuggestion& suggestion) {
  double confidence = ranker_->Rank(suggestion);
  return confidence >= threshold_;
};

}  // namespace modular
