// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/public/lib/async/cpp/operation.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "gtest/gtest.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/gtest/test_with_loop.h"
#include "peridot/public/lib/async/cpp/future.h"

namespace modular {
namespace {

class TestContainer : public OperationContainer {
 public:
  TestContainer() : weak_ptr_factory_(this) {}

  fxl::WeakPtr<OperationContainer> GetWeakPtr() override {
    return weak_ptr_factory_.GetWeakPtr();
  }

  int hold_count{0};
  int drop_count{0};
  int cont_count{0};
  OperationBase* last_held = nullptr;
  OperationBase* last_dropped = nullptr;

  void Hold(OperationBase* o) override {
    ++hold_count;
    last_held = o;
  }

  void Drop(OperationBase* o) override {
    ++drop_count;
    last_dropped = o;
  }

  void Cont() override { ++cont_count; }

  void PretendToDie() { weak_ptr_factory_.InvalidateWeakPtrs(); }

  using OperationContainer::InvalidateWeakPtrs;  // Promote for testing.
  using OperationContainer::Schedule;            // Promote for testing.

 private:
  fxl::WeakPtrFactory<OperationContainer> weak_ptr_factory_;
};

template <typename... Args>
class TestOperation : public Operation<Args...> {
 public:
  using ResultCall = std::function<void(Args...)>;
  TestOperation(std::function<void()> task, ResultCall done)
      : Operation<Args...>("Test Operation", std::move(done)), task_(task) {}

  void SayDone(Args... args) { this->Done(args...); }

 private:
  void Run() override { task_(); }

  std::function<void()> task_;
};

class OperationTest : public gtest::TestWithLoop {};

// Test the lifecycle of a single Operation:
// 1) Creating a new operation and adding it to a container
//    causes it to be scheduled (posted to the async task queue).
// 2) When the operation states it is done, the container is told
//    to schedule the next task after the result callback is called.
TEST_F(OperationTest, Lifecycle) {
  TestContainer container;
  bool op_ran = false;
  bool op_done = false;
  auto op =
      std::make_unique<TestOperation<>>([&op_ran] { op_ran = true; },
                                        [&op_done, &container] {
                                          op_done = true;

                                          // When our op is done, we
                                          // expect that the
                                          // OperationContainer that owns
                                          // it has already been told to
                                          // drop it, but we do not expect
                                          // Cont() to have been called.
                                          // That happens after the done
                                          // callback is executed.
                                          EXPECT_EQ(1, container.drop_count);
                                          EXPECT_EQ(0, container.cont_count);
                                        });

  // Add() calls Hold() on our OperationContainer implementation. Hold() is
  // supposed to manage the memory. Ours doesn't.
  // The task doesn't run until OperationContainer::Schedule is called with the
  // operation.
  // TODO(thatguy): TestContainer does not manage memory yet, so passing a
  // naked pointer backed by a unique_ptr<> is safe.
  container.Add(op.get());
  EXPECT_EQ(1, container.hold_count);
  EXPECT_EQ(op.get(), container.last_held);
  EXPECT_EQ(0, container.drop_count);
  EXPECT_EQ(0, container.cont_count);
  EXPECT_FALSE(op_ran);
  EXPECT_FALSE(op_done);

  // Add() does not enqueue the operation to be run, at least not in our
  // implementation of OperationContainer.
  RunLoopUntilIdle();
  EXPECT_FALSE(op_ran);
  EXPECT_FALSE(op_done);
  EXPECT_EQ(1, container.hold_count);
  EXPECT_EQ(0, container.drop_count);
  EXPECT_EQ(0, container.cont_count);

  // OperationContainer impls opt to call Schedule() when they are ready to
  // have an operation enqueued.
  container.Schedule(op.get());

  // So when we advance the async loop, we should see that it started running.
  RunLoopUntilIdle();
  EXPECT_TRUE(op_ran);
  EXPECT_FALSE(op_done);
  EXPECT_EQ(1, container.hold_count);
  EXPECT_EQ(0, container.drop_count);
  EXPECT_EQ(0, container.cont_count);

  // When the operation reports being done, we expect it to be dropped, our
  // done callback run, and the OperationContainer told to continue. These
  // happen in order. The order is verified in our done callback.
  op->SayDone();
  EXPECT_TRUE(op_ran);
  EXPECT_TRUE(op_done);
  EXPECT_EQ(1, container.hold_count);
  EXPECT_EQ(1, container.drop_count);
  EXPECT_EQ(op.get(), container.last_dropped);
  EXPECT_EQ(1, container.cont_count);
}

TEST_F(OperationTest, Lifecycle_ContainerGoesAway) {
  // In this test, we make the Operation think that its container's memory has
  // been cleaned up in its done callback. This manifests itself as the
  // Operation not invoking Cont() on the container.
  TestContainer container;
  bool op_ran = false;
  auto op = std::make_unique<TestOperation<>>(
      [&op_ran]() { op_ran = true; },
      [&container]() { container.PretendToDie(); });

  container.Add(op.get());
  container.Schedule(op.get());
  RunLoopUntilIdle();

  // When the operation reports being done, we expect it to be dropped, our
  // done callback run, and the OperationContainer told to continue. These
  // happen in order. The order is verified in our done callback.
  op->SayDone();
  EXPECT_TRUE(op_ran);
  EXPECT_EQ(1, container.hold_count);
  EXPECT_EQ(1, container.drop_count);
  EXPECT_EQ(op.get(), container.last_dropped);
  EXPECT_EQ(0, container.cont_count);
}

TEST_F(OperationTest, ResultsAreReceived) {
  // Show that when an operation calls Done(), its arguments are
  // passed to the done callback.
  TestContainer container;

  auto op = std::make_unique<TestOperation<int>>(
      [] {}, [](int result) { EXPECT_EQ(42, result); });

  container.Add(op.get());
  container.Schedule(op.get());
  RunLoopUntilIdle();
  op->SayDone(42);
}

class TestFlowTokenOperation : public Operation<int> {
 public:
  TestFlowTokenOperation(ResultCall done)
      : Operation("Test FlowToken Operation", std::move(done)) {}

  // |call_before_flow_dies| is invoked before the FlowToken goes out of scope.
  void SayDone(int result,
               std::function<void()> call_before_flow_dies = [] {}) {
    // When |flow| goes out of scope, it will call Done() for us.
    FlowToken flow{this, &result_};

    // Post the continuation of the operation to an async loop so that we
    // exercise the refcounting of FlowTokens.
    async::PostTask(async_get_default(),
                    [this, flow, result, call_before_flow_dies] {
                      result_ = result;
                      call_before_flow_dies();
                    });
  }

 private:
  void Run() override {}

  int result_;
};

TEST_F(OperationTest, Lifecycle_FlowToken) {
  // FlowTokens simply call Done() for you with whatever results you've pointed
  // it at. NOTE(thatguy): I haven't figured out a good way of testing the
  // refcount goodness of FlowTokens. That said, everything else in the world
  // will crash if they fail.
  TestContainer container;

  bool done_called{false};
  int result{0};
  auto op =
      std::make_unique<TestFlowTokenOperation>([&result, &done_called](int r) {
        done_called = true;
        result = r;
      });

  container.Add(op.get());
  container.Schedule(op.get());
  RunLoopUntilIdle();
  op->SayDone(42);
  // TestFlowTokenOperation posts to the async loop in SayDone(). We shouldn't
  // see Done() called until we run the loop and the FlowToken on the capture
  // list of the callback goes out of scope.
  EXPECT_FALSE(done_called);
  RunLoopUntilIdle();
  EXPECT_EQ(42, result);
}

TEST_F(OperationTest, Lifecycle_FlowToken_OperationGoesAway) {
  // Similar to Lifecycle_FlowToken, but FlowTokens have the behavior that if
  // their "owning" operation dies before they go out of scope (because, ie,
  // some callback is sitting in someone else's memory longer than the
  // operation lives), they don't call Done() on that operation.
  TestContainer container;

  bool done_called = false;
  auto op = std::make_unique<TestFlowTokenOperation>(
      [&done_called](int result) { done_called = true; });

  container.Add(op.get());
  container.Schedule(op.get());
  RunLoopUntilIdle();
  op->SayDone(42,
              [&container, &op]() { container.InvalidateWeakPtrs(op.get()); });
  RunLoopUntilIdle();
  EXPECT_FALSE(done_called);
}

TEST_F(OperationTest, OperationQueue) {
  // Here we test a specific implementation of OperationContainer
  // (OperationQueue), which should only allow one operation to "run" at a
  // given time. That means that operation #2 is not scheduled until operation
  // #1 finishes.
  OperationQueue container;

  // OperationQueue, unlike TestContainer, does manage its memory.
  bool op1_ran = false;
  bool op1_done = false;
  auto* op1 = new TestOperation<>([&op1_ran]() { op1_ran = true; },
                                  [&op1_done]() { op1_done = true; });

  bool op2_ran = false;
  bool op2_done = false;
  auto* op2 = new TestOperation<>([&op2_ran]() { op2_ran = true; },
                                  [&op2_done]() { op2_done = true; });

  container.Add(op1);
  container.Add(op2);

  // Nothing has run yet because we haven't run the async loop.
  EXPECT_FALSE(op1_ran);
  EXPECT_FALSE(op1_done);
  EXPECT_FALSE(op2_ran);
  EXPECT_FALSE(op2_done);

  // Running the loop we expect op1 to have run, but not completed.
  RunLoopUntilIdle();
  EXPECT_TRUE(op1_ran);
  EXPECT_FALSE(op1_done);
  EXPECT_FALSE(op2_ran);
  EXPECT_FALSE(op2_done);

  // But even if we run more, we do not expect op2 to run.
  RunLoopUntilIdle();
  EXPECT_TRUE(op1_ran);
  EXPECT_FALSE(op1_done);
  EXPECT_FALSE(op2_ran);
  EXPECT_FALSE(op2_done);

  // If op1 says it's Done(), we expect op2 to be queued, but not run yet.
  op1->SayDone();
  EXPECT_TRUE(op1_done);
  EXPECT_FALSE(op2_ran);
  EXPECT_FALSE(op2_done);

  // Running the loop again we expect op2 to start, but not completed.
  RunLoopUntilIdle();
  EXPECT_TRUE(op2_ran);
  EXPECT_FALSE(op2_done);
}

TEST_F(OperationTest, OperationCollection) {
  // OperationCollection starts all operations immediately and lets them run in
  // parallel.
  OperationCollection container;

  // OperationQueue, unlike TestContainer, does manage its memory.
  bool op1_ran = false;
  bool op1_done = false;
  auto* op1 = new TestOperation<>([&op1_ran]() { op1_ran = true; },
                                  [&op1_done]() { op1_done = true; });

  bool op2_ran = false;
  bool op2_done = false;
  auto* op2 = new TestOperation<>([&op2_ran]() { op2_ran = true; },
                                  [&op2_done]() { op2_done = true; });

  container.Add(op1);
  container.Add(op2);

  // Nothing has run yet because we haven't run the async loop.
  EXPECT_FALSE(op1_ran);
  EXPECT_FALSE(op1_done);
  EXPECT_FALSE(op2_ran);
  EXPECT_FALSE(op2_done);

  // Running the loop we expect op1 to have run, but not completed.
  RunLoopUntilIdle();
  EXPECT_TRUE(op1_ran);
  EXPECT_FALSE(op1_done);
  EXPECT_TRUE(op2_ran);
  EXPECT_FALSE(op2_done);
}

TEST_F(OperationTest, WrapFutureAsOperation_WithResult) {
  // Show that when we wrap a Future<> as an operation on a queue, it runs.
  bool op_did_start{};
  bool op_did_finish{};

  auto on_run = Future<>::Create(__PRETTY_FUNCTION__);
  auto done = on_run->Map([&]() -> int {
    EXPECT_FALSE(op_did_finish);
    op_did_start = true;
    return 10;
  });

  OperationCollection container;
  container.Add(WrapFutureAsOperation(
      on_run, done, std::function<void(int)>([&](int result) {
        EXPECT_EQ(10, result);
        op_did_finish = true;
      }),
      std::string(__PRETTY_FUNCTION__) + std::string("Operation")));

  RunLoopUntilIdle();
  EXPECT_TRUE(op_did_start);
  EXPECT_TRUE(op_did_finish);
}

TEST_F(OperationTest, WrapFutureAsOperation_WithoutResult) {
  // Show that when we wrap a Future<> as an operation on a queue, it runs.
  bool op_did_start{};
  bool op_did_finish{};

  auto on_run = Future<>::Create(__PRETTY_FUNCTION__);
  auto done = on_run->Then([&] {
    EXPECT_FALSE(op_did_finish);
    op_did_start = true;
  });

  OperationCollection container;
  container.Add(WrapFutureAsOperation(
      on_run, done, std::function<void()>([&] { op_did_finish = true; }),
      std::string(__PRETTY_FUNCTION__) + std::string("Operation")));

  RunLoopUntilIdle();
  EXPECT_TRUE(op_did_start);
  EXPECT_TRUE(op_did_finish);
}

}  // namespace
}  // namespace modular
