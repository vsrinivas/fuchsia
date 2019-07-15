// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story/systems/story_visibility_system.h"

#include <fuchsia/modular/storymodel/cpp/fidl.h>

#include "peridot/bin/sessionmgr/story/model/story_mutator.h"

namespace modular {

using fuchsia::modular::StoryVisibilityState;
using fuchsia::modular::storymodel::StoryModel;
using fuchsia::modular::storymodel::StoryModelMutation;

StoryVisibilitySystem::StoryVisibilitySystem(std::unique_ptr<StoryMutator> mutator)
    : mutator_(std::move(mutator)) {}

StoryVisibilitySystem::~StoryVisibilitySystem() {}

void StoryVisibilitySystem::RequestStoryVisibilityStateChange(
    const StoryVisibilityState visibility_state) {
  // Ignore any error resulting from this operation.
  mutator_->set_visibility_state(visibility_state);
}

}  // namespace modular
