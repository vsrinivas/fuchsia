// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_MODEL_TESTING_TEST_MUTATOR_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_MODEL_TESTING_TEST_MUTATOR_H_

#include <fuchsia/modular/storymodel/cpp/fidl.h>
#include <lib/fit/bridge.h>

#include "peridot/bin/sessionmgr/story/model/story_mutator.h"

namespace modular {

// A version of StoryMutator for use in tests.
//
// Collects all calls to StoryMutator.Execute() in public member
// |execute_calls|. Each element of |execute_calls| consists of:
//
// * |ExecuteCall.commands| are the StoryModelMutation commands that were
// issued in the Execute() call.
// * |ExecuteCall.completer| is used to complete the fit::promise<> that
// Execute() returns. The test author must call |completer.complete_ok()| or
// |completer.complete_error()| for any tasks blocked on the Execute() call to
// unblock.
class TestMutator : public StoryMutator {
 public:
  // Convenience factory allowing the caller to retain a pointer to the
  // TestMutator when constructing a class that accepts a StoryMutator as a
  // constructor argument.
  //
  // Usage:
  //
  // TestMutator* test_mutator;
  // Foo foo(TestMutator::Create(&test_mutator));
  static std::unique_ptr<StoryMutator> Create(TestMutator** ptr);

  fit::consumer<> ExecuteInternal(
      std::vector<fuchsia::modular::storymodel::StoryModelMutation> commands) override;

  struct ExecuteCall {
    fit::completer<> completer;
    std::vector<fuchsia::modular::storymodel::StoryModelMutation> commands;
  };
  std::vector<ExecuteCall> execute_calls;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_MODEL_TESTING_TEST_MUTATOR_H_
