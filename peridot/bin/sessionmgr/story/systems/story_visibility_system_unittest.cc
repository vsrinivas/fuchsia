// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story/systems/story_visibility_system.h"

#include <fuchsia/modular/storymodel/cpp/fidl.h>

#include "gmock/gmock.h"  // For EXPECT_THAT and matchers.
#include "gtest/gtest.h"
#include "peridot/bin/sessionmgr/story/model/testing/mutation_matchers.h"
#include "peridot/bin/sessionmgr/story/model/testing/test_mutator.h"

using fuchsia::modular::storymodel::StoryModel;
using fuchsia::modular::storymodel::StoryModelMutation;

namespace modular {
namespace {

class StoryVisibilitySystemTest : public ::testing::Test {
 protected:
  StoryVisibilitySystemTest() {
    system_ = std::make_unique<StoryVisibilitySystem>(TestMutator::Create(&mutator_));
  }

  std::unique_ptr<StoryVisibilitySystem> system_;
  TestMutator* mutator_;
};

TEST_F(StoryVisibilitySystemTest, All) {
  system_->RequestStoryVisibilityStateChange(fuchsia::modular::StoryVisibilityState::IMMERSIVE);

  EXPECT_EQ(1lu, mutator_->execute_calls.size());
  EXPECT_THAT(mutator_->execute_calls[0].commands,
              testing::ElementsAre(
                  IsSetVisibilityMutation(fuchsia::modular::StoryVisibilityState::IMMERSIVE)));
}

}  // namespace
}  // namespace modular
