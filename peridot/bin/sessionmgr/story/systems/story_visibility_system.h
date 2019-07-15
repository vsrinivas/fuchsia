// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_SYSTEMS_STORY_VISIBILITY_SYSTEM_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_SYSTEMS_STORY_VISIBILITY_SYSTEM_H_

#include <fuchsia/modular/cpp/fidl.h>

#include <memory>

#include "peridot/bin/sessionmgr/story/system.h"

namespace modular {

class StoryMutator;

// This sytem implements the policy for translating requests from Modules to
// change the visibility state of the story into the final visibility state of
// the story.
//
// The current policy is "allow all".
class StoryVisibilitySystem : public System {
 public:
  StoryVisibilitySystem(std::unique_ptr<StoryMutator> model_mutator);
  ~StoryVisibilitySystem() override;

  void RequestStoryVisibilityStateChange(
      const fuchsia::modular::StoryVisibilityState visibility_state);

 private:
  std::unique_ptr<StoryMutator> mutator_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_SYSTEMS_STORY_VISIBILITY_SYSTEM_H_
