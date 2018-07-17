// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/set_link_value_command_runner.h"

#include <lib/async/cpp/future.h>
#include <lib/fsl/vmo/strings.h>

#include "gtest/gtest.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/testing/test_with_ledger.h"

namespace modular {
namespace {

class SetLinkValueCommandRunnerTest : public testing::TestWithLedger {
 protected:
  std::unique_ptr<SessionStorage> MakeStorage(std::string ledger_page) {
    auto page_id = MakePageId(ledger_page);
    return std::make_unique<SessionStorage>(ledger_client(), page_id);
  }

  std::unique_ptr<SetLinkValueCommandRunner> MakeRunner(
      SessionStorage* const storage) {
    return std::make_unique<SetLinkValueCommandRunner>(storage);
  }

  std::unique_ptr<StoryStorage> GetStoryStorage(SessionStorage* const storage,
                                                std::string story_id) {
    std::unique_ptr<StoryStorage> story_storage;
    bool done{};
    storage->GetStoryStorage(story_id)->Then(
        [&](std::unique_ptr<StoryStorage> result) {
          FXL_DCHECK(result);
          story_storage = std::move(result);
          done = true;
        });
    RunLoopUntil([&] { return done; });

    return story_storage;
  }

  fidl::StringPtr CreateStory(SessionStorage* const storage) {
    auto future_story = storage->CreateStory(
        nullptr /* extra */, false /* is_kind_of_proto_story */);
    bool done{};
    fidl::StringPtr story_id;
    future_story->Then([&](fidl::StringPtr id, fuchsia::ledger::PageId) {
      done = true;
      story_id = std::move(id);
    });
    RunLoopUntil([&] { return done; });

    return story_id;
  }

  fuchsia::modular::LinkPath MakeLinkPath(const std::string& name) {
    fuchsia::modular::LinkPath path;
    path.link_name = name;
    return path;
  }

  fuchsia::modular::StoryCommand GetSetLinkValueCommand(
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
  auto storage = MakeStorage("page");
  auto runner = MakeRunner(storage.get());
  auto story_id = CreateStory(storage.get());
  auto story_storage = GetStoryStorage(storage.get(), story_id);
  bool done{};

  // Let's set a value.
  auto command = GetSetLinkValueCommand("link", "10");
  runner->Execute(story_id, std::move(command),
                  [&](fuchsia::modular::ExecuteResult result) {
                    ASSERT_EQ(fuchsia::modular::ExecuteStatus::OK,
                              result.status);
                    done = true;
                  });

  RunLoopUntil([&] { return done; });
  done = false;

  // Let's get the value.
  auto get_story_future = storage->GetStoryStorage(story_id);
  story_storage->GetLinkValue(MakeLinkPath("link"))
      ->Then([&](StoryStorage::Status status, fidl::StringPtr v) {
        EXPECT_EQ("10", *v);
        done = true;
      });

  RunLoopUntil([&] { return done; });
  done = false;

  // Mutate again.
  auto command2 = GetSetLinkValueCommand("link", "20");
  runner->Execute(story_id, std::move(command2),
                  [&](fuchsia::modular::ExecuteResult result) {
                    ASSERT_EQ(fuchsia::modular::ExecuteStatus::OK,
                              result.status);
                    done = true;
                  });

  RunLoopUntil([&] { return done; });
  done = false;

  // Let's get the value again, we should see the new one.
  story_storage->GetLinkValue(MakeLinkPath("link"))
      ->Then([&](StoryStorage::Status status, fidl::StringPtr v) {
        EXPECT_EQ("20", *v);
        done = true;
      });

  RunLoopUntil([&] { return done; });
}

TEST_F(SetLinkValueCommandRunnerTest, ExecuteInvalidStory) {
  auto storage = MakeStorage("page");
  auto runner = MakeRunner(storage.get());

  bool done{};
  auto command = GetSetLinkValueCommand("link", "10");
  runner->Execute("fake", std::move(command),
                  [&](fuchsia::modular::ExecuteResult result) {
                    EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_STORY_ID,
                              result.status);
                    done = true;
                  });

  RunLoopUntil([&] { return done; });
}

}  // namespace
}  // namespace modular
