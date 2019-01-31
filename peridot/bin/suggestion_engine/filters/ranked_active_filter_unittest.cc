// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/filters/ranked_active_filter.h"

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

class RankedActiveFilterTest : public ::testing::Test {
 public:
  void SetUp() override {
    filter = std::make_unique<RankedActiveFilter>(
        std::make_shared<ConfidenceRankingFeature>());
  }

 protected:
  std::unique_ptr<RankedActiveFilter> filter;
};

void AddTestRankedSuggestion(
    std::vector<std::unique_ptr<RankedSuggestion>>* const list,
    double confidence) {
  auto suggestion = std::make_unique<RankedSuggestion>();
  suggestion->confidence = confidence;
  list->push_back(std::move(suggestion));
}

TEST_F(RankedActiveFilterTest, Filter) {
  // Should filter all ranked suggestions for which the ranking feature
  // evaluates to 1 (max confidence).
  std::vector<std::unique_ptr<RankedSuggestion>> list;
  AddTestRankedSuggestion(&list, 0.1);
  AddTestRankedSuggestion(&list, 1.0);
  AddTestRankedSuggestion(&list, 0.5);
  AddTestRankedSuggestion(&list, 1.0);
  AddTestRankedSuggestion(&list, 0.9);
  AddTestRankedSuggestion(&list, 1.0);

  filter->Filter(&list);

  EXPECT_EQ(list.size(), 3u);
  EXPECT_EQ(list[0]->confidence, 0.1);
  EXPECT_EQ(list[1]->confidence, 0.5);
  EXPECT_EQ(list[2]->confidence, 0.9);
}

}  // namespace
}  // namespace modular
