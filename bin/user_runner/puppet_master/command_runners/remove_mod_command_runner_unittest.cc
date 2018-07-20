// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/user_runner/puppet_master/command_runners/remove_mod_command_runner.h"

#include <lib/async/cpp/future.h>

#include "gtest/gtest.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/testing/test_with_ledger.h"

namespace modular {
namespace {

class RemoveModCommandRunnerTest : public testing::TestWithLedger {
 protected:
  std::unique_ptr<SessionStorage> MakeStorage(std::string ledger_page) {
    auto page_id = MakePageId(ledger_page);
    return std::make_unique<SessionStorage>(ledger_client(), page_id);
  }

  std::unique_ptr<RemoveModCommandRunner> MakeRunner(
      SessionStorage* const storage) {
    return std::make_unique<RemoveModCommandRunner>(storage);
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

  fidl::VectorPtr<fidl::StringPtr> MakeModulePath(fidl::StringPtr path) {
    fidl::VectorPtr<fidl::StringPtr> module_path;
    module_path.push_back(path);
    return module_path;
  }

  fuchsia::modular::ModuleData InitModuleData(
      StoryStorage* const story_storage,
      fidl::VectorPtr<fidl::StringPtr> path) {
    fuchsia::modular::ModuleData module_data;
    module_data.module_path = std::move(path);
    module_data.intent = std::make_unique<fuchsia::modular::Intent>();
    module_data.module_stopped = false;

    bool done{};
    story_storage->WriteModuleData(std::move(module_data))->Then([&] {
      done = true;
    });
    RunLoopUntil([&] { return done; });
    return module_data;
  }
};

TEST_F(RemoveModCommandRunnerTest, Execute) {
  auto storage = MakeStorage("page");
  auto runner = MakeRunner(storage.get());
  auto story_id = CreateStory(storage.get());
  auto story_storage = GetStoryStorage(storage.get(), story_id);

  auto mod_name = MakeModulePath("mod");
  InitModuleData(story_storage.get(), mod_name.Clone());

  fuchsia::modular::RemoveMod remove_mod;
  remove_mod.mod_name = mod_name.Clone();
  fuchsia::modular::StoryCommand command;
  command.set_remove_mod(std::move(remove_mod));

  bool done{};
  runner->Execute(story_id, std::move(command),
                  [&](fuchsia::modular::ExecuteResult result) {
                    EXPECT_EQ(fuchsia::modular::ExecuteStatus::OK,
                              result.status);
                    EXPECT_EQ(story_id, result.story_id);
                    done = true;
                  });
  RunLoopUntil([&] { return done; });

  done = false;
  story_storage->ReadModuleData(std::move(mod_name))
      ->Then([&](fuchsia::modular::ModuleDataPtr module_data) {
        EXPECT_TRUE(module_data->module_stopped);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(RemoveModCommandRunnerTest, ExecuteNoModuleData) {
  auto storage = MakeStorage("page");
  auto runner = MakeRunner(storage.get());
  auto story_id = CreateStory(storage.get());

  auto mod_name = MakeModulePath("mod");
  fuchsia::modular::RemoveMod remove_mod;
  remove_mod.mod_name = std::move(mod_name);
  fuchsia::modular::StoryCommand command;
  command.set_remove_mod(std::move(remove_mod));

  bool done{};
  runner->Execute(
      story_id, std::move(command),
      [&](fuchsia::modular::ExecuteResult result) {
        EXPECT_EQ(fuchsia::modular::ExecuteStatus::INVALID_MOD, result.status);
        EXPECT_EQ(result.error_message, "No module data for given name.");
        EXPECT_EQ(story_id, result.story_id);
        done = true;
      });

  RunLoopUntil([&] { return done; });
}

TEST_F(RemoveModCommandRunnerTest, ExecuteInvalidStory) {
  auto storage = MakeStorage("page");
  auto runner = MakeRunner(storage.get());

  bool done{};
  fuchsia::modular::RemoveMod remove_mod;
  fuchsia::modular::StoryCommand command;
  command.set_remove_mod(std::move(remove_mod));
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
