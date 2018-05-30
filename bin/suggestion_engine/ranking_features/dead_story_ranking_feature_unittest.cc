// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/dead_story_ranking_feature.h"
#include "gtest/gtest.h"

namespace fuchsia {
namespace modular {
namespace {

class DeadStoryRankingFeatureTest : public ::testing::Test {
 protected:
  DeadStoryRankingFeature focused_story_feature;
  UserInput query;
};

// Creates the values from a context query to mock the modules in a focused
// story based on which this ranking feature computes its value.
void SetRunningStoryContextUpdate(fidl::VectorPtr<ContextValue>& context_update) {
  ContextValue value;
  value.meta.story = StoryMetadata::New();
  value.meta.story->id = "running_story";
  context_update.push_back(std::move(value));
}

TEST_F(DeadStoryRankingFeatureTest, RunningStoryAndAffinity) {
  Proposal proposal;
  proposal.story_affinity = true;
  SuggestionPrototype prototype;
  prototype.story_id = "running_story";
  prototype.proposal = std::move(proposal);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<ContextValue> context_update;
  SetRunningStoryContextUpdate(context_update);
  focused_story_feature.UpdateContext(context_update);
  double value = focused_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 1.0);
}

TEST_F(DeadStoryRankingFeatureTest, RunningButNoAffinity) {
  Proposal proposal;
  SuggestionPrototype prototype;
  prototype.story_id = "running_story";
  prototype.proposal = std::move(proposal);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<ContextValue> context_update;
  SetRunningStoryContextUpdate(context_update);
  focused_story_feature.UpdateContext(context_update);
  double value = focused_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.0);
}

TEST_F(DeadStoryRankingFeatureTest, NotRunningAndNoAffinity) {
  Proposal proposal;
  SuggestionPrototype prototype;
  prototype.story_id = "other_story";
  prototype.proposal = std::move(proposal);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<ContextValue> context_update;
  SetRunningStoryContextUpdate(context_update);
  focused_story_feature.UpdateContext(context_update);
  double value = focused_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.0);
}

TEST_F(DeadStoryRankingFeatureTest, NotRunningStoryAndAffinity) {
  Proposal proposal;
  proposal.story_affinity = true;
  SuggestionPrototype prototype;
  prototype.story_id = "other_story";
  prototype.proposal = std::move(proposal);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<ContextValue> context_update;
  SetRunningStoryContextUpdate(context_update);
  focused_story_feature.UpdateContext(context_update);
  double value = focused_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.0);
}

}  // namespace
}  // namespace modular
}  // namespace fuchsia
