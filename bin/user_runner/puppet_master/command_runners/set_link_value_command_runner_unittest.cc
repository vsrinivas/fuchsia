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
};

// On an empty story, it sets a link with a value, then updates it. Each time
// verifying that the link value is the expected one.
TEST_F(SetLinkValueCommandRunnerTest, Execute) {
  auto storage = MakeSessionStorage("page");
  auto runner = MakeRunner();
  auto story_id = CreateStory(storage.get());
  auto story_storage = GetStoryStorage(storage.get(), story_id);
  bool done{};

  // Let's set a value.
  auto command = MakeSetLinkValueCommand("link", "10");
  runner->Execute(story_id, story_storage.get(), std::move(command),
                  [&](fuchsia::modular::ExecuteResult result) {
                    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                              result.status);
                    done = true;
                  });
  RunLoopUntil([&] { return done; });
  done = false;

  // Let's get the value.
  EXPECT_EQ("10", GetLinkValue(story_storage.get(), "link"));

  // Mutate again.
  auto command2 = MakeSetLinkValueCommand("link", "20");
  runner->Execute(story_id, story_storage.get(), std::move(command2),
                  [&](fuchsia::modular::ExecuteResult result) {
                    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                              result.status);
                    done = true;
                  });
  RunLoopUntil([&] { return done; });
  done = false;

  // Let's get the value again, we should see the new one.
  EXPECT_EQ("20", GetLinkValue(story_storage.get(), "link"));
}

}  // namespace
}  // namespace modular
