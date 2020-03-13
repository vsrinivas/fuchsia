// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story/model/apply_mutations.h"

#include "lib/fostr/fidl/fuchsia/modular/storymodel/formatting.h"
#include "src/lib/syslog/cpp/logger.h"

using fuchsia::modular::StoryState;
using fuchsia::modular::StoryVisibilityState;
using fuchsia::modular::storymodel::StoryModel;
using fuchsia::modular::storymodel::StoryModelMutation;

namespace modular {

namespace {

void ApplySetVisibilityState(const StoryVisibilityState visibility_state, StoryModel* story_model) {
  story_model->set_visibility_state(visibility_state);
}

void ApplySetRuntimeState(const StoryState story_state, StoryModel* story_model) {
  story_model->set_runtime_state(story_state);
}

}  // namespace

StoryModel ApplyMutations(const StoryModel& current_model,
                          const std::vector<StoryModelMutation>& commands) {
  StoryModel new_model;
  fidl::Clone(current_model, &new_model);

  for (const auto& command : commands) {
    switch (command.Which()) {
      case StoryModelMutation::Tag::kSetVisibilityState:
        ApplySetVisibilityState(command.set_visibility_state(), &new_model);
        break;
      case StoryModelMutation::Tag::kSetRuntimeState:
        ApplySetRuntimeState(command.set_runtime_state(), &new_model);
        break;
      case StoryModelMutation::Tag::Invalid:
        FX_LOGS(FATAL) << "Encountered invalid StoryModelMutation: " << command;
    }
  }

  return new_model;
}

}  // namespace modular
