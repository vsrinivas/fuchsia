// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story/model/story_mutator.h"

#include <vector>

using fuchsia::modular::storymodel::StoryModelMutation;

namespace modular {

StoryMutator::StoryMutator() = default;
StoryMutator::~StoryMutator() = default;

fit::consumer<> StoryMutator::set_runtime_state(fuchsia::modular::StoryState state) {
  std::vector<StoryModelMutation> commands(1);
  commands[0].set_set_runtime_state(state);
  return ExecuteInternal(std::move(commands));
}

fit::consumer<> StoryMutator::set_visibility_state(fuchsia::modular::StoryVisibilityState state) {
  std::vector<StoryModelMutation> commands(1);
  commands[0].set_set_visibility_state(state);
  return ExecuteInternal(std::move(commands));
}

}  // namespace modular
