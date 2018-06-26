// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/dead_story_ranking_feature.h"

#include <fuchsia/modular/cpp/fidl.h>

namespace modular {

DeadStoryRankingFeature::DeadStoryRankingFeature() {}

DeadStoryRankingFeature::~DeadStoryRankingFeature() = default;

double DeadStoryRankingFeature::ComputeFeatureInternal(
    const fuchsia::modular::UserInput& query,
    const RankedSuggestion& ranked_suggestion) {
  bool story_affinity = ranked_suggestion.prototype->proposal.story_affinity;
  const auto& story_id = ranked_suggestion.prototype->story_id;

  // fuchsia::modular::Proposal not tied to any story.
  if (!story_affinity || story_id.empty()) {
    return kMinConfidence;
  }

  // TODO(miguelfrde): cache ids of stories in context in an unordered_set for
  // average O(1) lookup.
  for (auto& context_value : *ContextValues()) {
    if (story_id == context_value.meta.story->id) {
      return kMinConfidence;
    }
  }
  return kMaxConfidence;
}

fuchsia::modular::ContextSelectorPtr
DeadStoryRankingFeature::CreateContextSelectorInternal() {
  // Get stories in context.
  auto selector = fuchsia::modular::ContextSelector::New();
  selector->type = fuchsia::modular::ContextValueType::STORY;
  return selector;
}

}  // namespace modular
