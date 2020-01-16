// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story/model/ledger_story_model_storage.h"

#include <fuchsia/modular/storymodel/cpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/bridge.h>
#include <lib/fit/function.h>
#include <lib/fit/single_threaded_executor.h>

#include "gtest/gtest.h"
#include "src/modular/bin/sessionmgr/story/model/apply_mutations.h"
#include "src/modular/bin/sessionmgr/story/model/testing/mutation_matchers.h"
#include "src/modular/lib/ledger_client/ledger_client.h"
#include "src/modular/lib/ledger_client/page_id.h"
#include "src/modular/lib/testing/test_with_ledger.h"

using fuchsia::modular::StoryState;
using fuchsia::modular::StoryVisibilityState;
using fuchsia::modular::storymodel::StoryModel;
using fuchsia::modular::storymodel::StoryModelMutation;

namespace modular_testing {
namespace {

// TODO: there is no good candidate for testing conflict resolution in the
// StoryModel as of yet. What would be good is, e.g.: setting a value on a
// ModuleModel while simultaneously deleting the entire entry.

class LedgerStoryModelStorageTest : public modular_testing::TestWithLedger {
 public:
  async::Executor executor;

  LedgerStoryModelStorageTest() : executor(dispatcher()) {}

  // Creates a new LedgerStoryModelStorage instance and returns:
  // If |ledger_client| is not specified the default client is used, otherwise
  // use the given client.
  //
  // 1) A unique_ptr to the new instance.
  // 2) A ptr to a vector of lists of StoryModelMutations observed from that
  // instance.
  // 3) A ptr to a StoryModel updated with the observed commands.
  std::tuple<std::unique_ptr<modular::StoryModelStorage>,
             std::vector<std::vector<StoryModelMutation>>*, StoryModel*>
  Create(std::string page_id, std::string device_id,
         modular::LedgerClient* ledger_client = nullptr) {
    // If not client is specified, use the default client.
    if (!ledger_client) {
      ledger_client = this->ledger_client();
    }

    auto storage = std::make_unique<modular::LedgerStoryModelStorage>(
        ledger_client, modular::MakePageId(page_id), device_id);

    auto observed_commands = observed_mutations_.emplace(observed_mutations_.end());
    auto observed_model = observed_models_.emplace(observed_models_.end());
    storage->SetObserveCallback([=](std::vector<StoryModelMutation> commands) {
      *observed_model = modular::ApplyMutations(*observed_model, commands);
      observed_commands->push_back(std::move(commands));
    });
    return std::make_tuple(std::move(storage), &*observed_commands, &*observed_model);
  }

  // This is broken out into its own function because we use C++ structured
  // bindings to capture the result of Create() above. These cannot be
  // implicitly captured in lambdas without more verbose syntax. This function
  // converts the binding into a real variable which is possible to capture.
  void RunLoopUntilNumMutationsObserved(
      std::vector<std::vector<StoryModelMutation>>* observed_mutations, uint32_t n) {
    RunLoopUntil([&] { return observed_mutations->size() >= n; });
  }

 private:
  // A list (per StoryModelStorage instance) of the commands issued to each call
  // to StoryModelStorage.Observe().
  std::list<std::vector<std::vector<StoryModelMutation>>> observed_mutations_;
  std::list<StoryModel> observed_models_;
};

// Store some device-local values (runtime state, visibility state), and
// observe the values coming back to us.
TEST_F(LedgerStoryModelStorageTest, DeviceLocal_RoundTrip) {
  auto [storage, observed_mutations, observed_model] = Create("page1", "device1");

  std::vector<StoryModelMutation> commands(2);
  commands[0].set_set_runtime_state(StoryState::RUNNING);
  commands[1].set_set_visibility_state(StoryVisibilityState::IMMERSIVE);

  fit::result<> result;
  executor.schedule_task(
      storage->Execute(std::move(commands)).then([&](fit::result<>& r) { result = std::move(r); }));
  RunLoopUntil([&] { return !!result; });
  EXPECT_TRUE(result.is_ok());

  // We expect to see these values resulting in a notification from the ledger
  // eventually.
  RunLoopUntilNumMutationsObserved(observed_mutations, 1);
  EXPECT_EQ(1lu, observed_mutations->size());
  EXPECT_THAT(
      observed_mutations->at(0),
      ::testing::ElementsAre(modular::IsSetRuntimeStateMutation(StoryState::RUNNING),
                             modular::IsSetVisibilityMutation(StoryVisibilityState::IMMERSIVE)));

  // Now change only StoryState. We should see the result of our previous
  // change to StoryVisibilityState preserved.
  commands.resize(1);
  commands[0].set_set_runtime_state(StoryState::STOPPED);

  result = fit::result<>();
  executor.schedule_task(
      storage->Execute(std::move(commands)).then([&](fit::result<>& r) { result = std::move(r); }));
  RunLoopUntil([&] { return !!result; });
  EXPECT_TRUE(result.is_ok());

  RunLoopUntilNumMutationsObserved(observed_mutations, 2);
  EXPECT_EQ(2lu, observed_mutations->size());
  EXPECT_THAT(
      observed_mutations->at(1),
      ::testing::ElementsAre(modular::IsSetRuntimeStateMutation(StoryState::STOPPED),
                             modular::IsSetVisibilityMutation(StoryVisibilityState::IMMERSIVE)));
}

// Show that when we store values for two different device IDs in the same
// Ledger page, they do not cause any conflicts.
TEST_F(LedgerStoryModelStorageTest, DeviceLocal_DeviceIsolation) {
  auto [storage1, observed_mutations1, observed_model1] = Create("page1", "device1");
  auto second_ledger_connection = NewLedgerClient();
  auto [storage2, observed_mutations2, observed_model2] =
      Create("page1", "device2", second_ledger_connection.get());

  // Set runtime state to RUNNING on device1, and set visibility state to
  // IMMERSIVE on device2.
  {
    std::vector<StoryModelMutation> commands(1);
    commands[0].set_set_runtime_state(StoryState::RUNNING);
    executor.schedule_task(storage1->Execute(std::move(commands)));
  }
  {
    std::vector<StoryModelMutation> commands(1);
    commands[0].set_set_visibility_state(StoryVisibilityState::IMMERSIVE);
    executor.schedule_task(storage2->Execute(std::move(commands)));
  }

  RunLoopUntilNumMutationsObserved(observed_mutations1, 1);
  RunLoopUntilNumMutationsObserved(observed_mutations2, 1);

  EXPECT_TRUE(observed_model1->has_runtime_state());
  EXPECT_FALSE(observed_model1->has_visibility_state());
  EXPECT_TRUE(observed_model2->has_visibility_state());
  EXPECT_FALSE(observed_model2->has_runtime_state());
}

// Create two update tasks but schedule them out of order. We expect them to
// run in order.
TEST_F(LedgerStoryModelStorageTest, UpdatesAreSequential) {
  auto [storage, observed_mutations, observed_model] = Create("page", "device");

  std::vector<StoryModelMutation> commands(1);
  commands[0].set_set_runtime_state(StoryState::RUNNING);
  auto promise1 = storage->Execute(std::move(commands));

  commands.resize(1);
  commands[0].set_set_runtime_state(StoryState::STOPPING);
  auto promise2 = storage->Execute(std::move(commands));

  executor.schedule_task(std::move(promise2));
  RunLoopUntilIdle();  // For good measure.
  executor.schedule_task(std::move(promise1));

  RunLoopUntilNumMutationsObserved(observed_mutations, 2);
  EXPECT_EQ(StoryState::STOPPING, observed_model->runtime_state());
}

// When Load() is called, read what is stored in the Ledger back out and
// expect to see commands that represent that state through the storage
// observer.
TEST_F(LedgerStoryModelStorageTest, Load) {
  StoryModel expected_model;
  {
    auto [storage, observed_mutations, observed_model] = Create("page", "device");

    std::vector<StoryModelMutation> commands(2);
    commands[0].set_set_runtime_state(StoryState::RUNNING);
    commands[1].set_set_visibility_state(StoryVisibilityState::IMMERSIVE);
    // TODO(thatguy): As we add more StoryModelMutations, add more lines here.
    executor.schedule_task(storage->Execute(std::move(commands)));
    RunLoopUntilNumMutationsObserved(observed_mutations, 1);
    expected_model = std::move(*observed_model);
  }

  auto [storage, observed_mutations, observed_model] = Create("page", "device");

  bool done{false};
  executor.schedule_task(storage->Load().then([&](fit::result<>&) { done = true; }));
  RunLoopUntil([&] { return done; });
  EXPECT_TRUE(fidl::Equals(expected_model, *observed_model));
}

}  // namespace
}  // namespace modular_testing
