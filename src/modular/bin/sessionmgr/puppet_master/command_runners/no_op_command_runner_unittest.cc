// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/puppet_master/command_runners/no_op_command_runner.h"

#include <gtest/gtest.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

class NoOpCommandRunnerTest : public modular_testing::TestWithSessionStorage {
 public:
  void SetUp() override {
    modular_testing::TestWithSessionStorage::SetUp();
    session_storage_ = MakeSessionStorage();
    runner_ = MakeRunner();
    story_id_ = session_storage_->CreateStory("story", /*annotations=*/{});
    story_storage_ = GetStoryStorage(session_storage_.get(), story_id_);
  }

 protected:
  std::unique_ptr<NoOpCommandRunner> MakeRunner() { return std::make_unique<NoOpCommandRunner>(); }

  fuchsia::modular::StoryCommand MakeSetLinkValueCommand(const std::string& path_name,
                                                         const std::string& value) {
    fsl::SizedVmo vmo;
    fsl::VmoFromString(value, &vmo);
    fuchsia::modular::SetLinkValue set_link_value;
    set_link_value.path.link_name = "link";
    set_link_value.value = std::make_unique<fuchsia::mem::Buffer>(std::move(vmo).ToTransport());
    fuchsia::modular::StoryCommand command;
    command.set_set_link_value(std::move(set_link_value));
    return command;
  }

  std::unique_ptr<SessionStorage> session_storage_;
  std::shared_ptr<StoryStorage> story_storage_;
  std::unique_ptr<NoOpCommandRunner> runner_;
  std::string story_id_;
};

TEST_F(NoOpCommandRunnerTest, Execute) {
  // SetLinkValue is deprecated and results in NoOpCommandRunner being used.
  auto command = MakeSetLinkValueCommand("some-path", "some-value");
  bool done{};
  runner_->Execute(story_id_, story_storage_.get(), std::move(command),
                   [&](fuchsia::modular::ExecuteResult result) {
                     EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK, result.status);
                     done = true;
                   });
  RunLoopUntil([&] { return done; });
}

}  // namespace
}  // namespace modular
