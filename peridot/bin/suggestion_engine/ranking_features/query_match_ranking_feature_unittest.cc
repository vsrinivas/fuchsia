// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/query_match_ranking_feature.h"

#include "gtest/gtest.h"

namespace modular {
namespace {

class QueryMatchRankingFeatureTest : public ::testing::Test {
 protected:
  QueryMatchRankingFeature query_match_feature;
};

TEST_F(QueryMatchRankingFeatureTest, TestComputeFeatureRelated) {
  fuchsia::modular::SuggestionDisplay display;
  display.headline = "play bar by foo";
  fuchsia::modular::Proposal proposal;
  proposal.display = std::move(display);
  SuggestionPrototype prototype;
  prototype.proposal = std::move(proposal);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fuchsia::modular::UserInput query;
  query.text = "play bar";

  double value = query_match_feature.ComputeFeature(query, suggestion);
  EXPECT_NEAR(value, 0.533333, 0.00001);
}

TEST_F(QueryMatchRankingFeatureTest, TestComputeFeatureUnrelated) {
  fuchsia::modular::SuggestionDisplay display;
  display.headline = "play bar by foo";
  fuchsia::modular::Proposal proposal;
  proposal.display = std::move(display);
  SuggestionPrototype prototype;
  prototype.proposal = std::move(proposal);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fuchsia::modular::UserInput query;
  query.text = "open chat";

  double value = query_match_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, kMinConfidence);
}

}  // namespace
}  // namespace modular
