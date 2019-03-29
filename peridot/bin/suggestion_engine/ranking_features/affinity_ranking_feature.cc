// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/ranking_features/affinity_ranking_feature.h"

#include <lib/fsl/types/type_converters.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/join_strings.h>

namespace modular {

namespace {

bool MatchesMod(const fuchsia::modular::ModuleAffinity& affinity,
                const std::vector<std::string>& mod_path,
                const std::string& affinity_story) {
  // If the stories are not the same return early.
  if (affinity.story_name != affinity_story) {
    return false;
  }

  // Since module_paths are composed using the name of the parent view (note
  // this is how it's working today, but not how it should be expected to work.
  // Module names are opaque identifiers we shouldn't rely on for this kind of
  // logic), we compare the one in focused with the affinity we are interested.
  // Exmaple:
  //  - In focus: a/b/c
  //  - Affinity: a/b
  // Since the name is the same up to a/b, then we have higher confidence.
  // TODO(miguelfrde): rather than using this in a boolean fashion, return a
  // meaningful confidence that represents this behavior and supports cases such
  // as a/b/c vs a/d. For the case above it could be 0.66 and for the case of
  // a/d 0.33.
  for (uint32_t i = 0; i < mod_path.size(); i++) {
    if (i >= affinity.module_name.size()) {
      break;
    }
    if (mod_path.at(i) != affinity.module_name.at(i)) {
      return false;
    }
  }

  return true;
}

}  // namespace

AffinityRankingFeature::AffinityRankingFeature() {}

AffinityRankingFeature::~AffinityRankingFeature() = default;

double AffinityRankingFeature::ComputeFeatureInternal(
    const fuchsia::modular::UserInput& query,
    const RankedSuggestion& suggestion) {
  const auto& proposal = suggestion.prototype->proposal;
  if (proposal.affinity.empty()) {
    return kMaxConfidence;
  }
  for (const auto& context_value : *ContextValues()) {
    const auto& affinity_story_id = context_value.meta.story->id;
    for (const auto& affinity : proposal.affinity) {
      if (affinity.is_story_affinity() &&
          affinity_story_id == affinity.story_affinity().story_name) {
        return kMaxConfidence;
      }
      if (affinity.is_module_affinity() &&
          MatchesMod(affinity.module_affinity(), context_value.meta.mod->path,
                     affinity_story_id)) {
        return kMaxConfidence;
      }
    }
  }
  return kMinConfidence;
}

fuchsia::modular::ContextSelectorPtr
AffinityRankingFeature::CreateContextSelectorInternal() {
  // Get currently focused mod and in focused story.
  auto selector = fuchsia::modular::ContextSelector::New();
  selector->type = fuchsia::modular::ContextValueType::MODULE;
  selector->meta = fuchsia::modular::ContextMetadata::New();
  selector->meta->story = fuchsia::modular::StoryMetadata::New();
  selector->meta->story->focused = fuchsia::modular::FocusedState::New();
  selector->meta->story->focused->state =
      fuchsia::modular::FocusedStateState::FOCUSED;
  selector->meta->mod = fuchsia::modular::ModuleMetadata::New();
  selector->meta->mod->focused = fuchsia::modular::FocusedState::New();
  selector->meta->mod->focused->state =
      fuchsia::modular::FocusedStateState::FOCUSED;
  return selector;
}

}  // namespace modular
