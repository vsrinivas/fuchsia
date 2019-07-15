// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_MODEL_APPLY_MUTATIONS_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_MODEL_APPLY_MUTATIONS_H_

#include <fuchsia/modular/storymodel/cpp/fidl.h>

namespace modular {

// Returns a new StoryModel which is the result of applying |commands| to
// |current_model|.
fuchsia::modular::storymodel::StoryModel ApplyMutations(
    const fuchsia::modular::storymodel::StoryModel& current_model,
    const std::vector<fuchsia::modular::storymodel::StoryModelMutation>& commands);

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_MODEL_APPLY_MUTATIONS_H_
