// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/actor/actor_base.h"

#include <lib/fpromise/bridge.h>
#include <lib/zx/eventpair.h>

#include <future>
#include <iostream>
#include <memory>

#include <gtest/gtest.h>

namespace camera {
namespace {

class TestActorA : public actor::ActorBase {
 public:
  // Constructor for simple tests.
  TestActorA(async_dispatcher_t* dispatcher) : ActorBase(dispatcher, scope_) {}

  // Constructor for tests where TestActorB calls into TestActorA indirectly.
  TestActorA(async_dispatcher_t* dispatcher,
             std::function<fpromise::promise<void>(int)> set_B_state,
             std::function<fpromise::promise<int>()> get_B_state)
      : ActorBase(dispatcher, scope_), set_B_state_(set_B_state), get_B_state_(get_B_state) {}

  // Asynchronously sets the actor state.
  fpromise::promise<void> SetState(int state) {
    fpromise::bridge<void> bridge;
    Schedule([this, state, completer = std::move(bridge.completer)]() mutable {
      std::cout << "Actor A SetState running." << std::endl;
      state_ = state;
      completer.complete_ok();
    });
    std::cout << "Actor A SetState scheduled." << std::endl;
    return bridge.consumer.promise();
  }

  // Returns a promise that will hold the state after it is fetched asynchronously.
  fpromise::promise<int> GetState() {
    fpromise::bridge<int> bridge;
    Schedule([this, completer = std::move(bridge.completer)]() mutable {
      std::cout << "Actor A GetState running." << std::endl;
      completer.complete_ok(state_);
    });
    std::cout << "Actor A GetState scheduled." << std::endl;
    return bridge.consumer.promise();
  }

  // fpromise::future only works within the context of another promise handler. This version of the
  // method can be used within a test to synchronously check the state value.
  std::future<int> GetStateStd() {
    std::promise<int> sp;
    std::future<int> f = sp.get_future();
    Schedule([this, sp = std::move(sp)]() mutable {
      std::cout << "Actor A GetStateStd running." << std::endl;
      sp.set_value(state_);
    });
    std::cout << "Actor A GetStateStd scheduled." << std::endl;
    return f;
  }

  // Takes an eventpair and will update the actor state when the eventpair signals closed. Used to
  // test that WaitOnce works correctly.
  void UpdateStateOnSignal(zx::eventpair eventpair, int new_state) {
    WaitOnce(eventpair.release(), ZX_EVENTPAIR_PEER_CLOSED,
             [this, new_state](zx_status_t status, const zx_packet_signal_t* signal) {
               state_ = new_state;
             });
  }

  // Used to test that an actor can schedule things on another actor and wait for the result, even
  // if the other actor is running on the same async loop.
  fpromise::promise<void> FetchAndSetBState() {
    fpromise::bridge<void> bridge;
    Schedule([this, completer = std::move(bridge.completer)]() mutable {
      return get_B_state_()
          .and_then([this](int& stateA) { return set_B_state_(stateA + 1337); })
          .and_then([completer = std::move(completer)]() mutable { completer.complete_ok(); });
    });
    return bridge.consumer.promise();
  }

  // Used to test that an actor can schedule a chain of promises on itself and wait for them to
  // complete before moving on.
  fpromise::promise<void> ScheduleSomethingOnSelf() {
    fpromise::bridge<void> bridge;
    Schedule([this, completer = std::move(bridge.completer)]() mutable {
      return FetchAndSetBState().and_then(
          [completer = std::move(completer)]() mutable { completer.complete_ok(); });
    });
    return bridge.consumer.promise();
  }

  // This is exactly the kind of function that should not exist in a real actor, but we have in the
  // test to demonstrate when state modifications are actually supposed to happen.
  int GetStateImmediateForTest() { return state_; }

 private:
  int state_ = 0;

  std::function<fpromise::promise<void>(int)> set_B_state_;
  std::function<fpromise::promise<int>()> get_B_state_;

  fpromise::scope scope_;
};

class TestActorB : public actor::ActorBase {
 public:
  TestActorB(async_dispatcher_t* dispatcher, TestActorA& actor_a)
      : ActorBase(dispatcher, scope_), actor_a_(actor_a) {}

  // Asynchronously sets the actor state.
  fpromise::promise<void> SetState(int state) {
    fpromise::bridge<void> bridge;
    Schedule([this, state, completer = std::move(bridge.completer)]() mutable {
      std::cout << "Actor B SetState running." << std::endl;
      state_ = state;
      completer.complete_ok();
    });
    std::cout << "Actor B SetState scheduled." << std::endl;
    return bridge.consumer.promise();
  }

  // Returns a promise that will hold the state after it is fetch asynchronously.
  fpromise::promise<int> GetState() {
    fpromise::bridge<int> bridge;
    Schedule([this, completer = std::move(bridge.completer)]() mutable {
      std::cout << "Actor B GetState running." << std::endl;
      completer.complete_ok(state_);
    });
    std::cout << "Actor B GetState scheduled." << std::endl;
    return bridge.consumer.promise();
  }

  // fpromise::future only works within the context of another promise handler. This version of the
  // method can be used within a test to synchronously check the state value.
  std::future<int> GetStateStd() {
    std::promise<int> sp;
    std::future<int> f = sp.get_future();
    Schedule([this, sp = std::move(sp)]() mutable {
      std::cout << "Actor B GetStateStd running." << std::endl;
      sp.set_value(state_);
    });
    std::cout << "Actor B GetStateStd scheduled." << std::endl;
    return f;
  }

  // Schedule something on this actor, which schedules something on actor_a, which in turn schedules
  // something else on actor_a, which in turn schedules something back on this actor before the
  // whole chain completes.
  void StartSchedulingCycle() { Schedule(actor_a_.ScheduleSomethingOnSelf()); }

  // This is exactly the kind of function that should not exist in a real actor, but we have in the
  // test to demonstrate when state modifications are actually supposed to happen.
  int GetStateImmediateForTest() { return state_; }

 private:
  int state_ = 0;

  TestActorA& actor_a_;

  fpromise::scope scope_;
};

TEST(ActorBase, BasicSchedulingTest) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  TestActorA actor(loop.dispatcher());

  actor.SetState(1337);

  EXPECT_EQ(0, actor.GetStateImmediateForTest());

  std::future<int> future_state = actor.GetStateStd();

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(1337, future_state.get());
}

TEST(ActorBase, WaitOnceTest) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  TestActorA actor(loop.dispatcher());

  zx::eventpair eventpair_end0;
  zx::eventpair eventpair_end1;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0u, &eventpair_end0, &eventpair_end1));

  actor.UpdateStateOnSignal(std::move(eventpair_end1), 1337);
  eventpair_end0.reset();

  EXPECT_EQ(0, actor.GetStateImmediateForTest());

  ASSERT_EQ(ZX_OK, loop.RunUntilIdle());

  std::future<int> future_state = actor.GetStateStd();

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(1337, future_state.get());
}

TEST(ActorBase, ACallsIntoB) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  // Creates two actors which can communicate. Actor A can call into ActorB via these lambdas that
  // are passed in at construction. Actor B can call into Actor A directly but that is not used in
  // this test.
  std::unique_ptr<TestActorB> actorBPtr;
  TestActorA actorA(
      loop.dispatcher(), [&actorBPtr](int state) { return actorBPtr->SetState(state); },
      [&actorBPtr]() { return actorBPtr->GetState(); });
  actorBPtr = std::make_unique<TestActorB>(loop.dispatcher(), actorA);

  // Schedules promises on actorA which schedule promises on actorB to fetch actorB state (0) and
  // then set actorB state state after adding 1337.
  actorA.FetchAndSetBState();

  EXPECT_EQ(0, actorBPtr->GetStateImmediateForTest());

  // Run the async loop to let the scheduled things happen.
  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());
  std::cout << "First RunUntilIdle done." << std::endl;

  // Grab actorB state to check that it worked.
  std::future<int> future_state = actorBPtr->GetStateStd();

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(1337, future_state.get());
}

TEST(ActorBase, ScheduleCycleTest) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  // Creates two actors which can communicate. Actor A can call into ActorB via these lambdas that
  // are passed in at construction. Actor B can call into Actor A directly. This mimics a few
  // situations we have in production.
  std::unique_ptr<TestActorB> actorBPtr;
  TestActorA actorA(
      loop.dispatcher(), [&actorBPtr](int state) { return actorBPtr->SetState(state); },
      [&actorBPtr]() { return actorBPtr->GetState(); });
  actorBPtr = std::make_unique<TestActorB>(loop.dispatcher(), actorA);

  // Schedules a cycle of promises to ensure it all resolves properly:
  // actorB -> actorA -> ActorA -> actorB
  actorBPtr->StartSchedulingCycle();

  EXPECT_EQ(0, actorBPtr->GetStateImmediateForTest());

  // Run the async loop to let the scheduled things happen.
  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());
  std::cout << "First RunUntilIdle done." << std::endl;

  // Grab actorB state to check that it worked.
  std::future<int> future_state = actorBPtr->GetStateStd();

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(1337, future_state.get());
}

}  // namespace
}  // namespace camera
