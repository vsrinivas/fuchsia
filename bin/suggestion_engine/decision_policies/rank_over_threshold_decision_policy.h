// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_DECISION_POLICIES_RANK_OVER_THRESHOLD_DECISION_POLICY_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_DECISION_POLICIES_RANK_OVER_THRESHOLD_DECISION_POLICY_H_

#include "peridot/bin/suggestion_engine/decision_policies/decision_policy.h"
#include "peridot/bin/suggestion_engine/rankers/ranker.h"

namespace modular {

namespace {
constexpr double kDefaultThreshold = 1.0;
}  // namespace

// Base class for performing a decision on some value.
class RankOverThresholdDecisionPolicy : public DecisionPolicy {
 public:
  RankOverThresholdDecisionPolicy(std::unique_ptr<Ranker> ranker,
                                  double threshold = kDefaultThreshold);
  ~RankOverThresholdDecisionPolicy() override;

  bool Accept(const RankedSuggestion& suggestion) override;

 private:
  const std::unique_ptr<Ranker> ranker_;

  const double threshold_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_DECISION_POLICIES_RANK_OVER_THRESHOLD_DECISION_POLICY_H_
