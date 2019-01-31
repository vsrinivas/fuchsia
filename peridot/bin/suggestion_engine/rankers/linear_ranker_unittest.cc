// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/rankers/linear_ranker.h"

#include "gtest/gtest.h"
#include "peridot/bin/suggestion_engine/ranking_features/ranking_feature.h"

namespace modular {
namespace {

class ConstantRankingFeature : public RankingFeature {
 public:
  ConstantRankingFeature(double value) : value_(value) {}

 private:
  double ComputeFeatureInternal(const fuchsia::modular::UserInput& query,
                                const RankedSuggestion& suggestion) override {
    return value_;
  };

  double value_;
};

class LinearRankerTest : public ::testing::Test {
 protected:
  fuchsia::modular::UserInput query_;
  RankedSuggestion suggestion_;
  LinearRanker ranker_;
};

TEST_F(LinearRankerTest, Rank) {
  // Performs a simple ranking operation with all positive values.
  // Operation: (1*1 + 4*0.5)/(1 + 4) = 0.6
  ranker_.AddRankingFeature(1, std::make_shared<ConstantRankingFeature>(1.0));
  ranker_.AddRankingFeature(4, std::make_shared<ConstantRankingFeature>(0.5));
  EXPECT_EQ(ranker_.Rank(query_, suggestion_), 0.6);
}

TEST_F(LinearRankerTest, RankIgnoreNegativeWeigthsForNormalization) {
  // Checks that negative weights are ignored for the normalization factor.
  // Operation: (4*0.5 - 1*1) / 4;
  ranker_.AddRankingFeature(4, std::make_shared<ConstantRankingFeature>(0.5));
  ranker_.AddRankingFeature(-1, std::make_shared<ConstantRankingFeature>(1.0));
  EXPECT_EQ(ranker_.Rank(query_, suggestion_), 0.25);
}

TEST_F(LinearRankerTest, RankZeroIfNegative) {
  // Makes the linear combination of ranking features and weights zero if it is
  // less than zero.
  // Operation: (-1*0.5 + 2*0.1)/2 = -0.3 => 0
  ranker_.AddRankingFeature(-1, std::make_shared<ConstantRankingFeature>(0.5));
  ranker_.AddRankingFeature(2, std::make_shared<ConstantRankingFeature>(0.1));
  EXPECT_EQ(ranker_.Rank(query_, suggestion_), 0.0);
}

}  // namespace
}  // namespace modular
