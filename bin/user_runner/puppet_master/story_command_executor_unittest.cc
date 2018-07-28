// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/story_command_executor.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "gtest/gtest.h"

namespace modular {
namespace {

using fuchsia::modular::ExecuteResult;
using fuchsia::modular::ExecuteStatus;
using fuchsia::modular::StoryCommand;

class TestStoryCommandExecutor : public StoryCommandExecutor {
 public:
  void SetExecuteReturnResult(ExecuteStatus status,
                              fidl::StringPtr error_message) {
    result_.status = status;
    result_.error_message = error_message;
  }

  int execute_count{0};
  fidl::StringPtr last_story_id;
  std::vector<StoryCommand> last_commands;

 private:
  // |StoryCommandExecutor|
  void ExecuteCommandsInternal(
      fidl::StringPtr story_id, std::vector<StoryCommand> commands,
      std::function<void(ExecuteResult)> done) override {
    ++execute_count;
    last_story_id = story_id;
    last_commands = std::move(commands);
    ExecuteResult result;
    fidl::Clone(result_, &result);
    done(std::move(result));
  }

  ExecuteResult result_;
};

StoryCommand MakeRemoveModCommand(std::string mod_name) {
  StoryCommand command;
  fuchsia::modular::RemoveMod remove_mod;
  remove_mod.mod_name.push_back(mod_name);
  command.set_remove_mod(std::move(remove_mod));
  return command;
}

TEST(StoryCommandExecutorTest, ListenerSeesCommands) {
  TestStoryCommandExecutor executor;
  executor.SetExecuteReturnResult(ExecuteStatus::OK, "message");

  bool listener_called{false};
  auto auto_cancel = executor.AddListener(
      [&](const std::vector<StoryCommand>& commands, ExecuteResult result) {
        EXPECT_EQ(1lu, commands.size());
        EXPECT_EQ(ExecuteStatus::OK, result.status);
        EXPECT_EQ("message", result.error_message);
        listener_called = true;
      });

  bool execute_done{false};
  std::vector<StoryCommand> commands;
  commands.push_back(MakeRemoveModCommand("one"));
  executor.ExecuteCommands("story id", std::move(commands),
                           [&](ExecuteResult result) {
                             execute_done = true;
                             EXPECT_EQ(ExecuteStatus::OK, result.status);
                             EXPECT_EQ("message", result.error_message);
                           });
  EXPECT_TRUE(listener_called);
  EXPECT_TRUE(execute_done);

  // Now unregister the listener.
  auto_cancel.call();
  listener_called = false;
  execute_done = false;
  commands.push_back(MakeRemoveModCommand("one"));
  executor.ExecuteCommands("story id", std::move(commands),
                           [&](ExecuteResult result) { execute_done = true; });
  EXPECT_FALSE(listener_called);
  EXPECT_TRUE(execute_done);
}

}  // namespace
}  // namespace modular
