// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/affinity_ranking_feature.h"

#include "gtest/gtest.h"

namespace modular {
namespace {

class AffinityRankingFeatureTest : public ::testing::Test {
 protected:
  AffinityRankingFeature affinity_feature;
  fuchsia::modular::UserInput query;
};

// Creates the values from a context query to mock the modules in a focused
// story based on which this ranking feature computes its value.
void SetStoryAffinityContextUpdate(
    fidl::VectorPtr<fuchsia::modular::ContextValue>& context_update,
    const std::string& story_id) {
  fuchsia::modular::ContextValue value;
  value.meta.story = fuchsia::modular::StoryMetadata::New();
  value.meta.story->id = story_id;
  context_update.push_back(std::move(value));
}

void SetModuleAffinityContextUpdate(
    fidl::VectorPtr<fuchsia::modular::ContextValue>& context_update,
    const std::string& story_id, std::vector<std::string> mod_path) {
  fuchsia::modular::ContextValue value;
  value.meta.story = fuchsia::modular::StoryMetadata::New();
  value.meta.story->id = story_id;
  value.meta.mod = fuchsia::modular::ModuleMetadata::New();
  value.meta.mod->focused = fuchsia::modular::FocusedState::New();
  value.meta.mod->focused->state = fuchsia::modular::FocusedStateState::FOCUSED;
  for (auto& mod_path_part : mod_path) {
    value.meta.mod->path.push_back(mod_path_part);
  }
  context_update.push_back(std::move(value));
}

SuggestionPrototype BuildSuggestionPrototype() {
  fuchsia::modular::Proposal proposal;
  SuggestionPrototype prototype;
  prototype.source_url = "fake_url";
  prototype.proposal = std::move(proposal);
  return prototype;
}

SuggestionPrototype BuildSuggestionPrototypeWithStoryAffinity(
    std::string story_name) {
  auto prototype = BuildSuggestionPrototype();
  auto& proposal = prototype.proposal;
  fuchsia::modular::StoryAffinity story_affinity;
  story_affinity.story_name = story_name;
  fuchsia::modular::ProposalAffinity affinity;
  affinity.set_story_affinity(std::move(story_affinity));
  proposal.affinity.push_back(std::move(affinity));
  return prototype;
}

SuggestionPrototype BuildSuggestionPrototypeWithModuleAffinity(
    std::string story_name, std::string mod_name) {
  auto prototype = BuildSuggestionPrototype();
  auto& proposal = prototype.proposal;
  fuchsia::modular::ModuleAffinity module_affinity;
  module_affinity.story_name = story_name;
  module_affinity.module_name.push_back(mod_name);
  fuchsia::modular::ProposalAffinity affinity;
  affinity.set_module_affinity(std::move(module_affinity));
  proposal.affinity.push_back(std::move(affinity));
  return prototype;
}

TEST_F(AffinityRankingFeatureTest, ComputeFeatureStoryAffinity) {
  auto prototype = BuildSuggestionPrototypeWithStoryAffinity("affinity");
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetStoryAffinityContextUpdate(context_update, "affinity");
  affinity_feature.UpdateContext(context_update);

  double value = affinity_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 1.0);
}

TEST_F(AffinityRankingFeatureTest, ComputeFeatureNonAffinity) {
  auto prototype = BuildSuggestionPrototypeWithStoryAffinity("other_story");
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetStoryAffinityContextUpdate(context_update, "affinity");
  affinity_feature.UpdateContext(context_update);

  double value = affinity_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.0);
}

TEST_F(AffinityRankingFeatureTest, ComputeFeatureNonAffinityNoStoryAffinity) {
  auto prototype = BuildSuggestionPrototype();
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetStoryAffinityContextUpdate(context_update, "affinity");
  affinity_feature.UpdateContext(context_update);

  double value = affinity_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 1.0);
}

TEST_F(AffinityRankingFeatureTest, ComputeFeatureModAffinity) {
  auto prototype =
      BuildSuggestionPrototypeWithModuleAffinity("affinity", "mod_a");
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetModuleAffinityContextUpdate(context_update, "affinity", {"mod_a"});
  affinity_feature.UpdateContext(context_update);

  double value = affinity_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 1.0);
}

TEST_F(AffinityRankingFeatureTest, ComputeFeatureModNonAffinity) {
  auto prototype =
      BuildSuggestionPrototypeWithModuleAffinity("affinity", "mod_a");
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetModuleAffinityContextUpdate(context_update, "affinity", {"other_mod"});
  affinity_feature.UpdateContext(context_update);

  double value = affinity_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 0.0);
}

TEST_F(AffinityRankingFeatureTest, ComputeFeatureParentModAffinity) {
  // TODO(miguelfrde): instead of returning 1.0 we should update this to return
  // 0.5. There's a relevant note in the implementation.
  auto prototype =
      BuildSuggestionPrototypeWithModuleAffinity("affinity", "mod_a");
  RankedSuggestion suggestion;
  suggestion.prototype = &prototype;

  fidl::VectorPtr<fuchsia::modular::ContextValue> context_update;
  SetModuleAffinityContextUpdate(context_update, "affinity",
                                 {"mod_a", "mod_b"});
  affinity_feature.UpdateContext(context_update);

  double value = affinity_feature.ComputeFeature(query, suggestion);
  EXPECT_EQ(value, 1.0);
}

}  // namespace
}  // namespace modular
