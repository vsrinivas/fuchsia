// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/proposal_hint_ranking_feature.h"

#include "gtest/gtest.h"

namespace modular {
namespace {

class ProposalHintRankingFeatureTest : public ::testing::Test {
 protected:
  ProposalHintRankingFeature proposal_hint_feature;
  fuchsia::modular::UserInput query;
};

TEST_F(ProposalHintRankingFeatureTest, TestComputeFeature) {
  fuchsia::modular::Proposal proposal;
  proposal.confidence = 0.5;
  SuggestionPrototype prototype;
  prototype.proposal = std::move(proposal);
  prototype.source_url = "chat";
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;
  double value = proposal_hint_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.5);
}

}  // namespace
}  // namespace modular
