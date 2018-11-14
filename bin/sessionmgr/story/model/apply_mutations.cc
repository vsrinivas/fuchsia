// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/logging.h>

#include "peridot/bin/sessionmgr/story/model/apply_mutations.h"

using fuchsia::modular::StoryVisibilityState;
using fuchsia::modular::storymodel::StoryModel;
using fuchsia::modular::storymodel::StoryModelMutation;

namespace modular {

namespace {

void ApplySetVisibilityState(const StoryVisibilityState visibility_state, StoryModel* story_model) {
  story_model->set_visibility_state(visibility_state);
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
      default:
        FXL_LOG(FATAL) << "Unsupported StoryModelMutation: "
                       << fidl::ToUnderlying(command.Which());
    }
  }

  return new_model;
}

}  // namespace modular
