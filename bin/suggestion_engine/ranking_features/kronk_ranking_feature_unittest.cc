// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/kronk_ranking_feature.h"

#include "gtest/gtest.h"

namespace modular {
namespace {

class KronkRankingFeatureTest : public ::testing::Test {
 protected:
  KronkRankingFeature kronk_ranking_feature;
  fuchsia::modular::UserInput query;
};

TEST_F(KronkRankingFeatureTest, TestComputeFeatureKronk) {
  SuggestionPrototype prototype;
  prototype.source_url = "kronk";
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;
  double value = kronk_ranking_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, kMaxConfidence);
}

TEST_F(KronkRankingFeatureTest, TestComputeFeatureNonKronk) {
  SuggestionPrototype prototype;
  prototype.source_url = "chat";
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;
  double value = kronk_ranking_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, kMinConfidence);
}

}  // namespace
}  // namespace modular
