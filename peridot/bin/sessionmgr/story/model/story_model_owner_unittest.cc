// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/bridge.h>
#include <lib/fit/function.h>
#include <lib/fit/single_threaded_executor.h>

#include "lib/gtest/test_loop_fixture.h"
#include "peridot/bin/sessionmgr/story/model/story_model_owner.h"
#include "peridot/bin/sessionmgr/story/model/story_model_storage.h"

using fuchsia::modular::StoryVisibilityState;
using fuchsia::modular::storymodel::StoryModel;
using fuchsia::modular::storymodel::StoryModelMutation;

namespace modular {
namespace {

// This persistence system acts as a mock for calls to Execute(), and promotes
// Observe() from protected to public so that we can call it directly from the
// test body.
class TestModelStorage : public StoryModelStorage {
 public:
  struct ExecuteCall {
    std::vector<StoryModelMutation> commands;

    // Call this after a call to Execute() to complete the returned promise.
    fit::completer<> completer;
  };
  std::vector<ExecuteCall> calls;

  fit::promise<> Load() override { return fit::make_ok_promise(); }

  fit::promise<> Flush() override { return fit::make_ok_promise(); }

  fit::promise<> Execute(std::vector<StoryModelMutation> commands) override {
    fit::bridge<> bridge;

    // Store the arguments we got.
    ExecuteCall call{.commands = std::move(commands),
                     .completer = std::move(bridge.completer)};
    calls.push_back(std::move(call));

    return bridge.consumer.promise();
  }

  using StoryModelStorage::Observe;
};

class StoryModelOwnerTest : public ::gtest::TestLoopFixture {
 public:
  StoryModelOwnerTest() : TestLoopFixture() { ResetExecutor(); }

  std::unique_ptr<StoryModelOwner> Create(const std::string& story_name) {
    auto model_storage = std::make_unique<TestModelStorage>();
    model_storage_ = model_storage.get();

    auto owner = std::make_unique<StoryModelOwner>(story_name, executor_.get(),
                                                   std::move(model_storage));
    return owner;
  }

  void ResetExecutor() { executor_.reset(new async::Executor(dispatcher())); }

  TestModelStorage* model_storage() { return model_storage_; }

 private:
  std::unique_ptr<async::Executor> executor_;
  TestModelStorage* model_storage_;
};

TEST_F(StoryModelOwnerTest, SuccessfulMutate) {
  // Show that a single mutation flows through the StoryModelOwner to
  // StoryModelStorage, and then applies the resulting commands.
  auto owner = Create("test_name");

  auto mutator = owner->NewMutator();
  bool done{false};
  auto result_task =
      mutator->set_visibility_state(StoryVisibilityState::IMMERSIVE)
          .promise()
          .and_then([&] { done = true; })
          .or_else([] { FAIL(); });
  RunLoopUntilIdle();

  // The persistence system should have been called.
  ASSERT_EQ(1lu, model_storage()->calls.size());
  ASSERT_EQ(1lu, model_storage()->calls[0].commands.size());
  EXPECT_TRUE(model_storage()->calls[0].commands[0].is_set_visibility_state());
  EXPECT_EQ(StoryVisibilityState::IMMERSIVE,
            model_storage()->calls[0].commands[0].set_visibility_state());

  // Complete the pending persistence call.
  model_storage()->calls[0].completer.complete_ok();
  RunLoopUntilIdle();
  fit::run_single_threaded(std::move(result_task));
  EXPECT_TRUE(done);

  // The existing model hasn't changed, because the StoryModelStorage
  // has not heard back from its storage that the mutation occurred.
  auto observer = owner->NewObserver();
  EXPECT_EQ("test_name", observer->model().name());
  EXPECT_EQ(StoryVisibilityState::DEFAULT,
            observer->model().visibility_state());

  // Now dispatch mutations from the persistence system, and we should observe
  // that ApplyMutations() is invoked.
  model_storage()->Observe(std::move(model_storage()->calls[0].commands));

  // And the new model value that ApplyMutations returned should be reflected in
  // the owner.
  EXPECT_EQ(StoryVisibilityState::IMMERSIVE,
            observer->model().visibility_state());
}

TEST_F(StoryModelOwnerTest, FailedMutate) {
  auto owner = Create("test");
  auto mutator = owner->NewMutator();
  bool task_executed{false};
  bool saw_error{false};
  auto result_task =
      mutator->set_visibility_state(StoryVisibilityState::IMMERSIVE)
          .promise()
          .and_then([&] { task_executed = true; })
          .or_else([&] { saw_error = true; });
  RunLoopUntilIdle();
  model_storage()->calls[0].completer.complete_error();
  RunLoopUntilIdle();
  fit::run_single_threaded(std::move(result_task));
  EXPECT_FALSE(task_executed);
  EXPECT_TRUE(saw_error);
}

TEST_F(StoryModelOwnerTest, AbandonedMutate) {
  // If for some reason the underlying mutation is abandoned, we should observe
  // an error.
  auto owner = Create("test");
  auto mutator = owner->NewMutator();
  bool task_executed{false};
  bool saw_error{false};
  auto result_task =
      mutator->set_visibility_state(StoryVisibilityState::IMMERSIVE)
          .promise_or(fit::error())  // turn abandonment into an error.
          .and_then([&] { task_executed = true; })
          .or_else([&] { saw_error = true; });
  RunLoopUntilIdle();
  // Clearing the list of calls will destroy the completer, which has the
  // side-effect of abandoning the returned task.
  model_storage()->calls.clear();
  RunLoopUntilIdle();
  fit::run_single_threaded(std::move(result_task));
  EXPECT_FALSE(task_executed);
  EXPECT_TRUE(saw_error);
}

TEST_F(StoryModelOwnerTest, MutatorLifecycle_OwnerDestroyed) {
  // When the StoryModelOwner is destroyed but someone is still holding onto a
  // StoryMutator that mutator should return an error on Execute().
  auto owner = Create("test");
  auto mutator = owner->NewMutator();
  owner.reset();
  bool task_executed{false};
  bool saw_error{false};
  auto result_task =
      mutator->set_visibility_state(StoryVisibilityState::IMMERSIVE)
          .promise()
          .and_then([&] { task_executed = true; })
          .or_else([&] { saw_error = true; });
  RunLoopUntilIdle();
  fit::run_single_threaded(std::move(result_task));
  EXPECT_FALSE(task_executed);
  EXPECT_TRUE(saw_error);
}

TEST_F(StoryModelOwnerTest, ObserversAreNotified) {
  // One can create an observer and learn of the new state.
  auto owner = Create("test");
  auto mutator = owner->NewMutator();
  auto observer = owner->NewObserver();

  bool got_update1{false};
  observer->RegisterListener([&](const StoryModel& model) {
    got_update1 = true;
    EXPECT_EQ(StoryVisibilityState::IMMERSIVE, model.visibility_state());
  });

  // Another listener should also get the update!
  bool got_update2{false};
  observer->RegisterListener(
      [&](const StoryModel& model) { got_update2 = true; });

  // Also on another observer.
  auto observer2 = owner->NewObserver();
  bool got_update3{false};
  observer2->RegisterListener(
      [&](const StoryModel& model) { got_update3 = true; });

  std::vector<StoryModelMutation> commands;
  commands.resize(1);
  commands[0].set_set_visibility_state(StoryVisibilityState::IMMERSIVE);
  model_storage()->Observe(std::move(commands));
  RunLoopUntilIdle();
  EXPECT_TRUE(got_update1);
  EXPECT_TRUE(got_update2);
  EXPECT_TRUE(got_update3);
}

TEST_F(StoryModelOwnerTest, ObserversAreNotNotifiedOnNoChange) {
  // Observers aren't told when an observed mutation doesn't change the model.
  auto owner = Create("test");
  auto mutator = owner->NewMutator();
  auto observer = owner->NewObserver();

  bool got_update{false};
  observer->RegisterListener(
      [&](const StoryModel& model) { got_update = true; });

  std::vector<StoryModelMutation> commands;
  commands.resize(1);
  commands[0].set_set_visibility_state(StoryVisibilityState::DEFAULT);
  model_storage()->Observe(std::move(commands));
  RunLoopUntilIdle();
  EXPECT_FALSE(got_update);
}

TEST_F(StoryModelOwnerTest, ObserversLifecycle_ClientDestroyed) {
  // When the client destroys its observer object, it no longer receives
  // updates.
  auto owner = Create("test");
  auto mutator = owner->NewMutator();
  auto observer = owner->NewObserver();

  bool got_update{false};
  observer->RegisterListener(
      [&](const StoryModel& model) { got_update = true; });

  std::vector<StoryModelMutation> commands;
  commands.resize(1);
  commands[0].set_set_visibility_state(StoryVisibilityState::IMMERSIVE);
  model_storage()->Observe(std::move(commands));

  observer.reset();
  RunLoopUntilIdle();
  EXPECT_FALSE(got_update);
}

TEST_F(StoryModelOwnerTest, ObserversLifecycle_OwnerDestroyed) {
  // When the StoryModelOwner is destroyed, clients can learn of the fact by
  // using a fit::defer on the listener callback.
  auto owner = Create("test");
  auto mutator = owner->NewMutator();
  auto observer = owner->NewObserver();

  bool destroyed{false};
  observer->RegisterListener([defer = fit::defer([&] { destroyed = true; })](
                                 const StoryModel& model) {});

  owner.reset();
  EXPECT_TRUE(destroyed);

  // Explicitly destroy |observer| to ensure that its cleanup isn't affected by
  // |owner| being destroyed.
  observer.reset();
}

}  // namespace
}  // namespace modular
