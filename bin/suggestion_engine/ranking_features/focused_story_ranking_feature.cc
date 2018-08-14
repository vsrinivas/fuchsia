// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/focused_story_ranking_feature.h"

namespace modular {

FocusedStoryRankingFeature::FocusedStoryRankingFeature() {}

FocusedStoryRankingFeature::~FocusedStoryRankingFeature() = default;

double FocusedStoryRankingFeature::ComputeFeatureInternal(
    const fuchsia::modular::UserInput& query,
    const RankedSuggestion& suggestion) {
  if (!suggestion.prototype->proposal.story_affinity) {
    return kMaxConfidence;
  }
  for (auto& context_value : *ContextValues()) {
    const std::string focused_story_id = context_value.meta.story->id;
    if (focused_story_id == suggestion.prototype->proposal.story_name) {
      return kMaxConfidence;
    }
  }
  return kMinConfidence;
}

fuchsia::modular::ContextSelectorPtr
FocusedStoryRankingFeature::CreateContextSelectorInternal() {
  // Get currently focused story.
  auto selector = fuchsia::modular::ContextSelector::New();
  selector->type = fuchsia::modular::ContextValueType::STORY;
  selector->meta = fuchsia::modular::ContextMetadata::New();
  selector->meta->story = fuchsia::modular::StoryMetadata::New();
  selector->meta->story->focused = fuchsia::modular::FocusedState::New();
  selector->meta->story->focused->state =
      fuchsia::modular::FocusedStateState::FOCUSED;
  return selector;
}

}  // namespace modular
