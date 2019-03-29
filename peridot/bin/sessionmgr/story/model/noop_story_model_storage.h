// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_MODEL_NOOP_STORY_MODEL_STORAGE_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_MODEL_NOOP_STORY_MODEL_STORAGE_H_

#include <fuchsia/modular/storymodel/cpp/fidl.h>
#include <lib/fit/scope.h>
#include <src/lib/fxl/macros.h>

#include "peridot/bin/sessionmgr/story/model/story_model_storage.h"

namespace modular {

// Performs no persistence. Dispatch()es any requested mutations.
class NoopStoryModelStorage : public StoryModelStorage {
 public:
  NoopStoryModelStorage();
  ~NoopStoryModelStorage() override;

 private:
  fit::promise<> Load() override;
  fit::promise<> Flush() override;
  fit::promise<> Execute(
      std::vector<fuchsia::modular::storymodel::StoryModelMutation> commands)
      override;

  // When |scope_| is destroyed (which is when |this| is destructed), all
  // fit::promises we created in Mutate() will be abandoned. This is important
  // because those promises capture |this| in their handler functions.
  fit::scope scope_;

  FXL_DISALLOW_COPY_AND_ASSIGN(NoopStoryModelStorage);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_MODEL_NOOP_STORY_MODEL_STORAGE_H_
