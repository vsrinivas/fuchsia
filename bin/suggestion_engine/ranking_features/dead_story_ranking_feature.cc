// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <modular/cpp/fidl.h>

#include "peridot/bin/suggestion_engine/ranking_features/dead_story_ranking_feature.h"

namespace modular {

DeadStoryRankingFeature::DeadStoryRankingFeature() {}

DeadStoryRankingFeature::~DeadStoryRankingFeature() = default;

double DeadStoryRankingFeature::ComputeFeatureInternal(
    const UserInput& query, const RankedSuggestion& ranked_suggestion) {
  bool story_affinity = ranked_suggestion.prototype->proposal.story_affinity;
  if (!story_affinity) {
    return kMinConfidence;
  }
  // TODO(miguelfrde): cache ids of stories in context in an unordered_set for
  // average O(1) lookup.
  const auto& story_id = ranked_suggestion.prototype->story_id;
  for (auto& context_value : *ContextValues()) {
    if (story_id == context_value.meta.story->id) {
      return kMaxConfidence;
    }
  }
  return kMinConfidence;
}

ContextSelectorPtr
DeadStoryRankingFeature::CreateContextSelectorInternal() {
  // Get stories in context.
  auto selector = ContextSelector::New();
  selector->type = ContextValueType::STORY;
  return selector;
}

}  // namespace modular
