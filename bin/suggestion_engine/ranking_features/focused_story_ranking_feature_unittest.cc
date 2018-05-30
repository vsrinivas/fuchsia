// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/focused_story_ranking_feature.h"
#include "gtest/gtest.h"

namespace fuchsia {
namespace modular {
namespace {

class FocusedStoryRankingFeatureTest : public ::testing::Test {
 protected:
  FocusedStoryRankingFeature focused_story_feature;
  UserInput query;
};

// Creates the values from a context query to mock the modules in a focused
// story based on which this ranking feature computes its value.
void SetFocusedStoryContextUpdate(
    fidl::VectorPtr<ContextValue>& context_update) {
  ContextValue value;
  value.meta.story = StoryMetadata::New();
  value.meta.story->id = "focused_story";
  context_update.push_back(std::move(value));
}

TEST_F(FocusedStoryRankingFeatureTest, ComputeFeatureFocusedStory) {
  Proposal proposal;
  proposal.story_affinity = true;
  SuggestionPrototype prototype;
  prototype.story_id = "focused_story";
  prototype.proposal = std::move(proposal);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<ContextValue> context_update;
  SetFocusedStoryContextUpdate(context_update);
  focused_story_feature.UpdateContext(context_update);
  double value = focused_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 1.0);
}

TEST_F(FocusedStoryRankingFeatureTest, ComputeFeatureNonFocusedStory) {
  Proposal proposal;
  proposal.story_id = "other_story";
  proposal.story_affinity = true;
  SuggestionPrototype prototype;
  prototype.story_id = "other_story";
  prototype.proposal = std::move(proposal);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<ContextValue> context_update;
  SetFocusedStoryContextUpdate(context_update);
  focused_story_feature.UpdateContext(context_update);
  double value = focused_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.0);
}

TEST_F(FocusedStoryRankingFeatureTest,
       ComputeFeatureNonFocusedStoryNoStoryAffinity) {
  Proposal proposal;
  proposal.story_id = "other_story";
  proposal.story_affinity = false;
  SuggestionPrototype prototype;
  prototype.story_id = "other_story";
  prototype.proposal = std::move(proposal);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<ContextValue> context_update;
  SetFocusedStoryContextUpdate(context_update);
  focused_story_feature.UpdateContext(context_update);
  double value = focused_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 1.0);
}

}  // namespace
}  // namespace modular
}  // namespace fuchsia
