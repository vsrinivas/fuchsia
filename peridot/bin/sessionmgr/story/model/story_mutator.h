// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_MODEL_STORY_MUTATOR_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_MODEL_STORY_MUTATOR_H_

#include <fuchsia/modular/storymodel/cpp/fidl.h>
#include <lib/fit/bridge.h>
#include <src/lib/fxl/macros.h>

namespace modular {

// Implemented by and created by the StoryModelOwner.
//
// An instance of StoryMutator is provided to all clients that wish to perform
// mutations on a StoryModel. It is responsible for consuming mutation commands
// (exposed as methods publicly and converted to StoryModelMutation internally)
// and dispatching them be applied to a shared StoryModel instance.
//
// This is an interface in order to aid in testing clients that depend on
// StoryMutator.
class StoryMutator {
 public:
  StoryMutator();
  virtual ~StoryMutator();

  // The following mutators issue a single mutation instruction to
  // change the StoryModel.
  //
  // The returned fit::consumer<> will eventually be completed with the result
  // of the mutation operation, HOWEVER success does not guarantee that
  // observers of the StoryModel will see those same changes reflected, and thus
  // clients should NOT perform side-effects under that assumption.
  //
  // It IS safe to perform side-effects once mutations have been observed
  // through StoryObserver.
  //
  // A failure guarantees that the mutation was not applied and it is safe to
  // retry.

  // Sets the value of |StoryModel.runtime_state|.
  fit::consumer<> set_runtime_state(fuchsia::modular::StoryState state);

  // Sets the value of |StoryModel.visibility_state|.
  fit::consumer<> set_visibility_state(
      fuchsia::modular::StoryVisibilityState state);

 private:
  // Executes |commands| in order and in a single transaction.
  virtual fit::consumer<> ExecuteInternal(
      std::vector<fuchsia::modular::storymodel::StoryModelMutation>
          commands) = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryMutator);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_MODEL_STORY_MUTATOR_H_
