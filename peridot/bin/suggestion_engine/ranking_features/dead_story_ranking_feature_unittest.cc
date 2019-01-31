// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/dead_story_ranking_feature.h"

#include "gtest/gtest.h"

namespace modular {
namespace {

class DeadStoryRankingFeatureTest : public ::testing::Test {
 protected:
  fuchsia::modular::UserInput query;
  DeadStoryRankingFeature dead_story_feature;
};

// Creates the values from a context query to mock the modules in a focused
// story based on which this ranking feature computes its value.
void SetRunningStoryContextUpdate(
    fidl::VectorPtr<fuchsia::modular::ContextValue>& context_update) {
  fuchsia::modular::ContextValue value;
  value.meta.story = fuchsia::modular::StoryMetadata::New();
  value.meta.story->id = "running_story";
  context_update.push_back(std::move(value));
}

SuggestionPrototype BuildSuggestionPrototype(std::string story_name,
                                             bool has_story_affinity) {
  fuchsia::modular::Proposal proposal;
  if (has_story_affinity) {
    fuchsia::modular::StoryAffinity story_affinity;
    story_affinity.story_name = story_name;
    fuchsia::modular::ProposalAffinity affinity;
    affinity.set_story_affinity(std::move(story_affinity));
    proposal.affinity.push_back(std::move(affinity));
  }
  SuggestionPrototype prototype;
  prototype.source_url = "fake_url";
  prototype.proposal = std::move(proposal);
  return prototype;
}

TEST_F(DeadStoryRankingFeatureTest, RunningStoryAndAffinity) {
  auto prototype = BuildSuggestionPrototype("running_story", true);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetRunningStoryContextUpdate(context_update);
  dead_story_feature.UpdateContext(context_update);
  double value = dead_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.0);
}

TEST_F(DeadStoryRankingFeatureTest, RunningButNoAffinity) {
  auto prototype = BuildSuggestionPrototype("running_story", false);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetRunningStoryContextUpdate(context_update);
  dead_story_feature.UpdateContext(context_update);
  double value = dead_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.0);
}

TEST_F(DeadStoryRankingFeatureTest, NotRunningAndNoAffinity) {
  auto prototype = BuildSuggestionPrototype("dead_story", false);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetRunningStoryContextUpdate(context_update);
  dead_story_feature.UpdateContext(context_update);
  double value = dead_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.0);
}

TEST_F(DeadStoryRankingFeatureTest, NotRunningStoryAndAffinity) {
  auto prototype = BuildSuggestionPrototype("dead_story", true);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetRunningStoryContextUpdate(context_update);
  dead_story_feature.UpdateContext(context_update);
  double value = dead_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 1.0);
}

}  // namespace
}  // namespace modular
