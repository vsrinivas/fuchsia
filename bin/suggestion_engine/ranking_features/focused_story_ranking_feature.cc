// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/focused_story_ranking_feature.h"

namespace modular {

FocusedStoryRankingFeature::FocusedStoryRankingFeature() {}

FocusedStoryRankingFeature::~FocusedStoryRankingFeature() = default;

double FocusedStoryRankingFeature::ComputeFeatureInternal(
    const UserInput& query,
    const RankedSuggestion& suggestion) {
  if (!suggestion.prototype->proposal.story_affinity) {
    return kMaxConfidence;
  }
  for (auto& context_value : *ContextValues()) {
    const std::string focused_story_id = context_value.meta.story->id;
    if (focused_story_id == suggestion.prototype->story_id) {
      return kMaxConfidence;
    }
  }
  return kMinConfidence;
}

ContextSelectorPtr FocusedStoryRankingFeature::CreateContextSelectorInternal() {
  // Get currently focused story.
  auto selector = ContextSelector::New();
  selector->type = ContextValueType::STORY;
  selector->meta = ContextMetadata::New();
  selector->meta->story = StoryMetadata::New();
  selector->meta->story->focused = FocusedState::New();
  selector->meta->story->focused->state = FocusedStateState::FOCUSED;
  return selector;
}

}  // namespace modular
