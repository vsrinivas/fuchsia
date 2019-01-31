// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/decision_policies/rank_over_threshold_decision_policy.h"

#include "gtest/gtest.h"
#include "peridot/bin/suggestion_engine/rankers/ranker.h"

namespace modular {
namespace {

class TestRanker : public Ranker {
 public:
  double Rank(const fuchsia::modular::UserInput& query,
              const RankedSuggestion& suggestion) override {
    return suggestion.confidence;
  }
};

class RankOverThresholdDecisionPolicyTest : public ::testing::Test {
 public:
  void SetUp() override {
    decision_policy = std::make_unique<RankOverThresholdDecisionPolicy>(
        std::make_unique<TestRanker>(), 0.5);
  }

 protected:
  std::unique_ptr<RankOverThresholdDecisionPolicy> decision_policy;
};

TEST_F(RankOverThresholdDecisionPolicyTest, Accept) {
  RankedSuggestion suggestion;
  suggestion.confidence = 0.6;
  EXPECT_TRUE(decision_policy->Accept(suggestion));
  suggestion.confidence = 0.5;
  EXPECT_TRUE(decision_policy->Accept(suggestion));
}

TEST_F(RankOverThresholdDecisionPolicyTest, NotAccept) {
  RankedSuggestion suggestion;
  suggestion.confidence = 0.4;
  EXPECT_FALSE(decision_policy->Accept(suggestion));
}

}  // namespace
}  // namespace modular
