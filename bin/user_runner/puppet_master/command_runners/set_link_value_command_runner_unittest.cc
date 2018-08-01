// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/set_link_value_command_runner.h"

#include <lib/fsl/vmo/strings.h>

#include "gtest/gtest.h"
#include "peridot/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

class SetLinkValueCommandRunnerTest : public testing::TestWithSessionStorage {
 public:
  void SetUp() override {
    testing::TestWithSessionStorage::SetUp();
    session_storage_ = MakeSessionStorage("page");
    runner_ = MakeRunner();
    story_id_ = CreateStory(session_storage_.get());
    story_storage_ = GetStoryStorage(session_storage_.get(), story_id_);
  }

 protected:
  std::unique_ptr<SetLinkValueCommandRunner> MakeRunner() {
    return std::make_unique<SetLinkValueCommandRunner>();
  }

  fuchsia::modular::StoryCommand MakeSetLinkValueCommand(
      const std::string& path_name, const std::string& value) {
    fsl::SizedVmo vmo;
    fsl::VmoFromString(value, &vmo);
    fuchsia::modular::SetLinkValue set_link_value;
    set_link_value.path = MakeLinkPath(path_name);
    set_link_value.value =
        std::make_unique<fuchsia::mem::Buffer>(std::move(vmo).ToTransport());
    fuchsia::modular::StoryCommand command;
    command.set_set_link_value(std::move(set_link_value));
    return command;
  }

  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<StoryStorage> story_storage_;
  std::unique_ptr<SetLinkValueCommandRunner> runner_;
  std::string story_id_;
};

// On an empty story, it sets a link with a value, then updates it. Each time
// verifying that the link value is the expected one.
TEST_F(SetLinkValueCommandRunnerTest, Execute) {
  bool done{};

  // Let's set a value.
  auto command = MakeSetLinkValueCommand("link", "10");
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                               result.status);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
  done = false;

  // Let's get the value.
  EXPECT_EQ("10", GetLinkValue(story_storage_.get(), "link"));

  // Mutate again.
  auto command2 = MakeSetLinkValueCommand("link", "20");
  runner_->Execute(story_id_, story_storage_.get(), std::move(command2),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                               result.status);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
  done = false;

  // Let's get the value again, we should see the new one.
  EXPECT_EQ("20", GetLinkValue(story_storage_.get(), "link"));
}

TEST_F(SetLinkValueCommandRunnerTest, ExecuteInvalidJson) {
  bool done{};

  // Let's set a value.
  auto command = MakeSetLinkValueCommand("link", "10");
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                               result.status);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
  done = false;

  // Let's get the value.
  EXPECT_EQ("10", GetLinkValue(story_storage_.get(), "link"));

  // Mutate with invalid JSON.
  auto command2 = MakeSetLinkValueCommand("link", "x}");
  runner_->Execute(story_id_, story_storage_.get(), std::move(command2),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_COMMAND,
                               result.status);
                     EXPECT_EQ("Attempted to update link with invalid JSON",
                               result.error_message);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
  done = false;

  // Let's get the value again, we should see the original one.
  EXPECT_EQ("10", GetLinkValue(story_storage_.get(), "link"));
}

}  // namespace
}  // namespace modular
