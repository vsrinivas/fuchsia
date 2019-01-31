// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/filters/ranked_passive_filter.h"

#include "gtest/gtest.h"
#include "peridot/bin/suggestion_engine/ranking_features/ranking_feature.h"

namespace modular {
namespace {

class ConfidenceRankingFeature : public RankingFeature {
 public:
  ConfidenceRankingFeature() {}

 private:
  double ComputeFeatureInternal(const fuchsia::modular::UserInput& query,
                                const RankedSuggestion& suggestion) override {
    return suggestion.confidence;
  }
};

class RankedPassiveFilterTest : public ::testing::Test {
 public:
  void SetUp() override {
    filter = std::make_unique<RankedPassiveFilter>(
        std::make_shared<ConfidenceRankingFeature>());
  }

 protected:
  std::unique_ptr<RankedPassiveFilter> filter;
};

TEST_F(RankedPassiveFilterTest, FilterMaxConfidence) {
  auto suggestion = std::make_unique<RankedSuggestion>();
  suggestion->confidence = 1.0;
  EXPECT_TRUE(filter->Filter(suggestion));
}

TEST_F(RankedPassiveFilterTest, FilterOtherConfidence) {
  auto suggestion = std::make_unique<RankedSuggestion>();
  suggestion->confidence = 0.5;
  EXPECT_FALSE(filter->Filter(suggestion));

  auto suggestion2 = std::make_unique<RankedSuggestion>();
  suggestion2->confidence = 0.0;
  EXPECT_FALSE(filter->Filter(suggestion2));
}

}  // namespace
}  // namespace modular
