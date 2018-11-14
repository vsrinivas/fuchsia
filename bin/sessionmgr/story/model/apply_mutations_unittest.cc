// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/storymodel/cpp/fidl.h>
#include <lib/fit/bridge.h>
#include <lib/fit/function.h>
#include <lib/fit/single_threaded_executor.h>

#include "gtest/gtest.h"
#include "peridot/bin/sessionmgr/story/model/apply_mutations.h"

using fuchsia::modular::StoryVisibilityState;
using fuchsia::modular::storymodel::StoryModel;
using fuchsia::modular::storymodel::StoryModelMutation;

namespace modular {
namespace {

// Test a single StoryModelMutation.set_visibility_state command to change
// StoryModel.visibility_state.
TEST(ApplyMutationsTest, SingleMutation_set_visibility_state) {
  StoryModel before;
  *before.mutable_visibility_state() = StoryVisibilityState::DEFAULT;

  std::vector<StoryModelMutation> commands(1);
  commands[0].set_set_visibility_state(StoryVisibilityState::IMMERSIVE);
  auto result = ApplyMutations(before, commands);
  EXPECT_EQ(StoryVisibilityState::IMMERSIVE, *result.visibility_state());
}

// Test two StoryModelMutation.set_visibility_state commands to change
// StoryModel.visibility_state to one value and back. Tests that multiple
// commands in a list are applied in order.
TEST(ApplyMutationsTest,
     MultipleMutations_AppliedInOrder_set_visibility_state) {
  StoryModel before;

  std::vector<StoryModelMutation> commands(2);
  commands[0].set_set_visibility_state(StoryVisibilityState::IMMERSIVE);
  commands[1].set_set_visibility_state(StoryVisibilityState::DEFAULT);
  auto result = ApplyMutations(before, commands);
  EXPECT_EQ(StoryVisibilityState::DEFAULT, *result.visibility_state());
}

}  // namespace
}  // namespace modular
