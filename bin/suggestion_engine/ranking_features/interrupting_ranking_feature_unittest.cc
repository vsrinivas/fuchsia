// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/interrupting_ranking_feature.h"

#include "gtest/gtest.h"

namespace modular {
namespace {

class InterruptingRankingFeatureTest : public ::testing::Test {
 protected:
  InterruptingRankingFeature ranking_feature;
  fuchsia::modular::UserInput query;
};

TEST_F(InterruptingRankingFeatureTest, ComputeFeatureInterrupting) {
  RankedSuggestion suggestion;
  suggestion.interrupting = true;

  double value = ranking_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 1.0);
}

TEST_F(InterruptingRankingFeatureTest, ComputeFeatureNonInterrupting) {
  RankedSuggestion suggestion;
  suggestion.interrupting = false;

  double value = ranking_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.0);
}

}  // namespace
}  // namespace modular
