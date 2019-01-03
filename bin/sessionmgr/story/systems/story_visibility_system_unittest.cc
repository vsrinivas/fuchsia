// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/storymodel/cpp/fidl.h>

#include "gmock/gmock.h"  // For EXPECT_THAT and matchers.
#include "gtest/gtest.h"
#include "lib/fostr/fidl/fuchsia/modular/storymodel/formatting.h"
#include "peridot/bin/sessionmgr/story/model/story_mutator.h"
#include "peridot/bin/sessionmgr/story/systems/story_visibility_system.h"

using fuchsia::modular::storymodel::StoryModel;
using fuchsia::modular::storymodel::StoryModelMutation;

namespace modular {
namespace {

// TODO(thatguy): Move these matchers into a shared file.

// |arg| is a StoryModelMutation |expected| is a StoryVisibilitystate.
MATCHER_P(IsSetVisibilityMutation, expected, "") {
  *result_listener << "is set_visibility_state { "
                   << fidl::ToUnderlying(expected) << "}";
  if (!arg.is_set_visibility_state())
    return false;
  return expected == arg.set_visibility_state();
}

// TODO(thatguy): Move this test mutator into a shared file.
class TestMutator : public StoryMutator {
 public:
  fit::consumer<> ExecuteInternal(
      std::vector<StoryModelMutation> commands) override {
    fit::bridge<> bridge;
    ExecuteCall call{.completer = std::move(bridge.completer),
                     .commands = std::move(commands)};
    execute_calls.push_back(std::move(call));
    return std::move(bridge.consumer);
  }

  struct ExecuteCall {
    fit::completer<> completer;
    std::vector<StoryModelMutation> commands;
  };
  std::vector<ExecuteCall> execute_calls;
};

class StoryVisibilitySystemTest : public ::testing::Test {
 protected:
  StoryVisibilitySystemTest() {
    auto mutator = std::make_unique<TestMutator>();
    mutator_ = mutator.get();
    system_ = std::make_unique<StoryVisibilitySystem>(std::move(mutator));
  }

  std::unique_ptr<StoryVisibilitySystem> system_;
  TestMutator* mutator_;
};

TEST_F(StoryVisibilitySystemTest, All) {
  system_->RequestStoryVisibilityStateChange(
      fuchsia::modular::StoryVisibilityState::IMMERSIVE);

  EXPECT_EQ(1lu, mutator_->execute_calls.size());
  EXPECT_THAT(mutator_->execute_calls[0].commands,
              testing::ElementsAre(IsSetVisibilityMutation(
                  fuchsia::modular::StoryVisibilityState::IMMERSIVE)));
}

}  // namespace
}  // namespace modular
