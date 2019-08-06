// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/puppet_master/command_runners/set_kind_of_proto_story_option_command_runner.h"

#include <lib/fsl/vmo/strings.h>

#include "gtest/gtest.h"
#include "peridot/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

class SetKindOfProtoStoryOptionCommandRunnerTest : public testing::TestWithSessionStorage {
 public:
  void SetUp() override {
    testing::TestWithSessionStorage::SetUp();
    session_storage_ = MakeSessionStorage("page");
    runner_ = MakeRunner();
    story_id_ = CreateStory(session_storage_.get()).value_or("");
  }

 protected:
  std::unique_ptr<SetKindOfProtoStoryOptionCommandRunner> MakeRunner() {
    return std::make_unique<SetKindOfProtoStoryOptionCommandRunner>(session_storage_.get());
  }

  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<SetKindOfProtoStoryOptionCommandRunner> runner_;
  std::string story_id_;
};

TEST_F(SetKindOfProtoStoryOptionCommandRunnerTest, Execute) {
  fuchsia::modular::StoryCommand command;
  fuchsia::modular::SetKindOfProtoStoryOption set_kind_of_proto_story_option;
  bool done{};

  // Set a value.
  {
    set_kind_of_proto_story_option.value = true;
    command.set_set_kind_of_proto_story_option(std::move(set_kind_of_proto_story_option));
    runner_->Execute(story_id_, nullptr /* story_storage */, std::move(command),
                     [&](fuchsia::modular::ExecuteResult result) {
                       EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
                       done = true;
                     });
    RunLoopUntil([&] { return done; });
  }

  // Verify it was written.
  done = false;
  session_storage_->GetStoryData(story_id_)->Then(
      [&](fuchsia::modular::internal::StoryDataPtr data) {
        EXPECT_TRUE(data->story_options().kind_of_proto_story);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // Update the value.
  {
    set_kind_of_proto_story_option.value = false;
    command.set_set_kind_of_proto_story_option(std::move(set_kind_of_proto_story_option));
    done = false;
    runner_->Execute(story_id_, nullptr /* story_storage */, std::move(command),
                     [&](fuchsia::modular::ExecuteResult result) {
                       EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
                       done = true;
                     });
    RunLoopUntil([&] { return done; });
  }

  // Verify second value.
  done = false;
  session_storage_->GetStoryData(story_id_)->Then(
      [&](fuchsia::modular::internal::StoryDataPtr data) {
        EXPECT_FALSE(data->story_options().kind_of_proto_story);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

}  // namespace
}  // namespace modular
