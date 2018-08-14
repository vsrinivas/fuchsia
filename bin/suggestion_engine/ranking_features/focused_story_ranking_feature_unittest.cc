// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/focused_story_ranking_feature.h"

#include "gtest/gtest.h"

namespace modular {
namespace {

class FocusedStoryRankingFeatureTest : public ::testing::Test {
 protected:
  FocusedStoryRankingFeature focused_story_feature;
  fuchsia::modular::UserInput query;
};

// Creates the values from a context query to mock the modules in a focused
// story based on which this ranking feature computes its value.
void SetFocusedStoryContextUpdate(
    fidl::VectorPtr<fuchsia::modular::ContextValue>& context_update) {
  fuchsia::modular::ContextValue value;
  value.meta.story = fuchsia::modular::StoryMetadata::New();
  value.meta.story->id = "focused_story";
  context_update.push_back(std::move(value));
}

SuggestionPrototype BuildSuggestionPrototype(std::string story_name,
                                             bool story_affinity) {
  fuchsia::modular::Proposal proposal;
  proposal.story_affinity = story_affinity;
  proposal.story_name = story_name;
  SuggestionPrototype prototype;
  prototype.source_url = "fake_url";
  prototype.proposal = std::move(proposal);
  return prototype;
}

TEST_F(FocusedStoryRankingFeatureTest, ComputeFeatureFocusedStory) {
  auto prototype = BuildSuggestionPrototype("focused_story", true);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetFocusedStoryContextUpdate(context_update);
  focused_story_feature.UpdateContext(context_update);

  double value = focused_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 1.0);
}

TEST_F(FocusedStoryRankingFeatureTest, ComputeFeatureNonFocusedStory) {
  auto prototype = BuildSuggestionPrototype("other_story", true);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetFocusedStoryContextUpdate(context_update);
  focused_story_feature.UpdateContext(context_update);

  double value = focused_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.0);
}

TEST_F(FocusedStoryRankingFeatureTest,
       ComputeFeatureNonFocusedStoryNoStoryAffinity) {
  auto prototype = BuildSuggestionPrototype("other_story", false);
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetFocusedStoryContextUpdate(context_update);
  focused_story_feature.UpdateContext(context_update);

  double value = focused_story_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 1.0);
}

}  // namespace
}  // namespace modular
