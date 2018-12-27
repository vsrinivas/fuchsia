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
  const auto& proposal = ranked_suggestion.prototype->proposal;

  // Proposal not tied to any story.
  if (proposal.affinity.empty()) {
    return kMinConfidence;
  }

  // TODO(miguelfrde): cache ids of stories in context in an unordered_set for
  // average O(1) lookup.
  for (const auto& context_value : *ContextValues()) {
    const auto& story_name = context_value.meta.story->id;
    for (const auto& affinity : proposal.affinity) {
      switch (affinity.Which()) {
        case fuchsia::modular::ProposalAffinity::Tag::kModuleAffinity:
          if (story_name == affinity.module_affinity().story_name) {
            return kMinConfidence;
          }
          break;
        case fuchsia::modular::ProposalAffinity::Tag::kStoryAffinity:
          if (story_name == affinity.story_affinity().story_name) {
            return kMinConfidence;
          }
          break;
        default:
          break;
      }
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
