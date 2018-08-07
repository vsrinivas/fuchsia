// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/focus_mod_command_runner.h"

#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"

namespace modular {
namespace {

class FocusModCommandRunnerTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    focused_called_ = false;
    runner_ = std::make_unique<FocusModCommandRunner>(
        [&](fidl::StringPtr story_id,
            fidl::VectorPtr<fidl::StringPtr> mod_name) {
          focused_called_ = true;
        });
  }

  bool focused_called_{};
  std::unique_ptr<FocusModCommandRunner> runner_;
};

TEST_F(FocusModCommandRunnerTest, Focus) {
  fuchsia::modular::FocusMod focus_mod;
  focus_mod.mod_name.push_back("mod");
  fuchsia::modular::StoryCommand command;
  command.set_focus_mod(std::move(focus_mod));

  fuchsia::modular::ExecuteResult result;
  runner_->Execute("story1", nullptr /* story_storage */, std::move(command),
                   [&](fuchsia::modular::ExecuteResult execute_result) {
                     result = std::move(execute_result);
                   });

  RunLoopUntilIdle();
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
  EXPECT_TRUE(focused_called_);
}

TEST_F(FocusModCommandRunnerTest, FocusEmptyPath) {
  fuchsia::modular::FocusMod focus_mod;
  focus_mod.mod_name.resize(0);
  fuchsia::modular::StoryCommand command;
  command.set_focus_mod(std::move(focus_mod));

  fuchsia::modular::ExecuteResult result;
  runner_->Execute("story1", nullptr /* story_storage */, std::move(command),
                   [&](fuchsia::modular::ExecuteResult execute_result) {
                     result = std::move(execute_result);
                   });

  RunLoopUntilIdle();
  EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_COMMAND, result.status);
  EXPECT_EQ("No mod_name provided.", result.error_message);
  EXPECT_FALSE(focused_called_);
}

}  // namespace
}  // namespace modular
