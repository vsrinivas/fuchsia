// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/operation.h>

#include <lib/async/cpp/future.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <src/lib/fxl/memory/weak_ptr.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"

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
  std::unique_ptr<OperationBase> last_held = nullptr;
  OperationBase* last_dropped = nullptr;

  void Hold(std::unique_ptr<OperationBase> o) override {
    ++hold_count;
    last_held = std::move(o);
  }

  void Drop(OperationBase* o) override {
    ++drop_count;
    last_dropped = o;
  }

  void Cont() override { ++cont_count; }

  void ScheduleTask(fit::pending_task task) override {}

  void PretendToDie() { weak_ptr_factory_.InvalidateWeakPtrs(); }

  using OperationContainer::InvalidateWeakPtrs;  // Promote for testing.
  using OperationContainer::Schedule;            // Promote for testing.

 private:
  fxl::WeakPtrFactory<OperationContainer> weak_ptr_factory_;
};

template <typename... Args>
class TestOperation : public Operation<Args...> {
 public:
  using ResultCall = fit::function<void(Args...)>;

  TestOperation(fit::function<void()> task, ResultCall done)
      : Operation<Args...>("Test Operation", std::move(done)),
        task_(std::move(task)) {}

  void SayDone(Args... args) { this->Done(args...); }

 private:
  void Run() override { task_(); }

  fit::function<void()> task_;
};

class OperationTest : public gtest::TestLoopFixture {};

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
  auto weak_op = op->GetWeakPtr();
  container.Add(std::move(op));
  EXPECT_EQ(1, container.hold_count);
  EXPECT_EQ(weak_op.get(), container.last_held.get());
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
  container.Schedule(weak_op.get());

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
  if (weak_op) {
    static_cast<TestOperation<>*>(weak_op.get())->SayDone();
  }
  EXPECT_TRUE(op_ran);
  EXPECT_TRUE(op_done);
  EXPECT_EQ(1, container.hold_count);
  EXPECT_EQ(1, container.drop_count);
  EXPECT_EQ(weak_op.get(), container.last_dropped);
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

  auto weak_op = op->GetWeakPtr();
  container.Add(std::move(op));
  container.Schedule(weak_op.get());
  RunLoopUntilIdle();

  // When the operation reports being done, we expect it to be dropped, our
  // done callback run, and the OperationContainer told to continue. These
  // happen in order. The order is verified in our done callback.
  if (weak_op) {
    static_cast<TestOperation<>*>(weak_op.get())->SayDone();
  }
  EXPECT_TRUE(op_ran);
  EXPECT_EQ(1, container.hold_count);
  EXPECT_EQ(1, container.drop_count);
  EXPECT_EQ(weak_op.get(), container.last_dropped);
  EXPECT_EQ(0, container.cont_count);
}

TEST_F(OperationTest, ResultsAreReceived) {
  // Show that when an operation calls Done(), its arguments are
  // passed to the done callback.
  TestContainer container;

  auto op = std::make_unique<TestOperation<int>>(
      [] {}, [](int result) { EXPECT_EQ(42, result); });

  auto weak_op = op->GetWeakPtr();
  container.Add(std::move(op));
  container.Schedule(weak_op.get());
  RunLoopUntilIdle();
  if (weak_op) {
    static_cast<TestOperation<int>*>(weak_op.get())->SayDone(42);
  }
}

class TestFlowTokenOperation : public Operation<int> {
 public:
  TestFlowTokenOperation(ResultCall done)
      : Operation("Test FlowToken Operation", std::move(done)) {}

  // |call_before_flow_dies| is invoked before the FlowToken goes out of scope.
  void SayDone(
      int result, fit::function<void()> call_before_flow_dies = [] {}) {
    // When |flow| goes out of scope, it will call Done() for us.
    FlowToken flow{this, &result_};

    // Post the continuation of the operation to an async loop so that we
    // exercise the refcounting of FlowTokens.
    async::PostTask(async_get_default_dispatcher(),
                    [this, flow, result,
                     call_before_flow_dies = std::move(call_before_flow_dies)] {
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

  auto weak_op = op->GetWeakPtr();
  container.Add(std::move(op));
  container.Schedule(weak_op.get());
  RunLoopUntilIdle();
  if (weak_op) {
    static_cast<TestFlowTokenOperation*>(weak_op.get())->SayDone(42);
  }
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

  auto weak_op = op->GetWeakPtr();
  container.Add(std::move(op));
  container.Schedule(weak_op.get());
  RunLoopUntilIdle();
  if (weak_op) {
    static_cast<TestFlowTokenOperation*>(weak_op.get())
        ->SayDone(42, [&container, weak_op]() {
          container.InvalidateWeakPtrs(weak_op.get());
        });
  }
  RunLoopUntilIdle();
  EXPECT_FALSE(done_called);
}

TEST_F(OperationTest, OperationQueue) {
  // Here we test a specific implementation of OperationContainer
  // (OperationQueue), which should only allow one operation to "run" at a
  // given time.
  OperationQueue container;

  // OperationQueue, unlike TestContainer, does own the Operations.
  bool op1_ran = false;
  bool op1_done = false;
  auto op1 = std::make_unique<TestOperation<>>(
      [&op1_ran]() { op1_ran = true; }, [&op1_done]() { op1_done = true; });

  bool op3_ran = false;
  bool op3_done = false;
  auto op3 = std::make_unique<TestOperation<>>(
      [&op3_ran]() { op3_ran = true; }, [&op3_done]() { op3_done = true; });

  // We'll queue |op1|, then a fit::promise ("op2") and another Operation,
  // |op3|.
  auto weak_op1 = op1->GetWeakPtr();
  container.Add(std::move(op1));

  bool op2_ran = false;
  bool op2_done = false;
  fit::suspended_task suspended_op2;
  container.ScheduleTask(
      fit::make_promise([&](fit::context& c) -> fit::result<> {
        if (op2_ran == true) {
          op2_done = true;
          return fit::ok();
        }
        op2_ran = true;
        suspended_op2 = c.suspend_task();
        return fit::pending();
      }));

  container.Add(std::move(op3));

  // Nothing has run yet because we haven't run the async loop.
  EXPECT_FALSE(op1_ran);
  EXPECT_FALSE(op1_done);
  EXPECT_FALSE(op2_ran);
  EXPECT_FALSE(op2_done);
  EXPECT_FALSE(op3_ran);
  EXPECT_FALSE(op3_done);

  // Running the loop we expect op1 to have run, but not completed.
  RunLoopUntilIdle();
  EXPECT_TRUE(op1_ran);
  EXPECT_FALSE(op1_done);
  EXPECT_FALSE(op2_ran);
  EXPECT_FALSE(op2_done);
  EXPECT_FALSE(op3_ran);
  EXPECT_FALSE(op3_done);

  // But even if we run more, we do not expect any other ops to run.
  RunLoopUntilIdle();
  EXPECT_TRUE(op1_ran);
  EXPECT_FALSE(op1_done);
  EXPECT_FALSE(op2_ran);
  EXPECT_FALSE(op2_done);
  EXPECT_FALSE(op3_ran);
  EXPECT_FALSE(op3_done);

  // If op1 says it's Done(), we expect op2 to run.
  if (weak_op1) {
    static_cast<TestOperation<>*>(weak_op1.get())->SayDone();
  }
  RunLoopUntilIdle();
  EXPECT_TRUE(op1_done);
  EXPECT_TRUE(op2_ran);
  EXPECT_FALSE(op2_done);
  EXPECT_FALSE(op3_ran);
  EXPECT_FALSE(op3_done);

  // Running the loop again should do nothing, as op2 is still pending (until
  // we resume it manuall).
  RunLoopUntilIdle();
  EXPECT_TRUE(suspended_op2);
  EXPECT_FALSE(op2_done);
  EXPECT_FALSE(op3_ran);
  EXPECT_FALSE(op3_done);

  // Resume op2, and run the loop again. We expect op3 to start, but not
  // complete.
  suspended_op2.resume_task();
  RunLoopUntilIdle();
  EXPECT_TRUE(op2_done);
  EXPECT_TRUE(op3_ran);
  EXPECT_FALSE(op3_done);
}

TEST_F(OperationTest, OperationCollection) {
  // OperationCollection starts all operations immediately and lets them run in
  // parallel.
  OperationCollection container;

  // OperationQueue, unlike TestContainer, does manage its memory.
  bool op1_ran = false;
  bool op1_done = false;
  auto op1 = std::make_unique<TestOperation<>>(
      [&op1_ran]() { op1_ran = true; }, [&op1_done]() { op1_done = true; });

  bool op2_ran = false;
  bool op2_done = false;
  auto op2 = std::make_unique<TestOperation<>>(
      [&op2_ran]() { op2_ran = true; }, [&op2_done]() { op2_done = true; });

  container.Add(std::move(op1));
  container.Add(std::move(op2));
  bool op3_ran = false;
  container.ScheduleTask(fit::make_promise([&] { op3_ran = true; }));

  // Nothing has run yet because we haven't run the async loop.
  EXPECT_FALSE(op1_ran);
  EXPECT_FALSE(op1_done);
  EXPECT_FALSE(op2_ran);
  EXPECT_FALSE(op2_done);
  EXPECT_FALSE(op3_ran);

  // Running the loop we expect all ops to have run. TestOperations won't have
  // completed because they require us to call SayDone(). The fit::promise,
  // however, will have completed (it doesn't suspend).
  RunLoopUntilIdle();
  EXPECT_TRUE(op1_ran);
  EXPECT_FALSE(op1_done);
  EXPECT_TRUE(op2_ran);
  EXPECT_FALSE(op2_done);
  EXPECT_TRUE(op2_ran);
}

class TestOperationNotNullPtr : public Operation<> {
 public:
  TestOperationNotNullPtr(const FlowToken& parent_flow_token)
      : Operation("Test Operation on container is not nullptr", [] {}),
        parent_flow_token_(parent_flow_token) {}

 private:
  void Run() override { FlowToken flow{this}; }

  FlowToken parent_flow_token_;
};

class TestQueueNotNullPtr : public Operation<> {
 public:
  TestQueueNotNullPtr(ResultCall done)
      : Operation("TestQueueNotNullPtr", std::move(done)) {}

 private:
  void Run() override {
    // When |flow| goes out of scope, it will call Done() for us.
    FlowToken flow{this};
    operation_queue_.Add(
        std::make_unique<TestOperationNotNullPtr>(std::move(flow)));
  }

  OperationQueue operation_queue_;
};

class TestCollectionNotNullPtr : public Operation<> {
 public:
  TestCollectionNotNullPtr(ResultCall done)
      : Operation("TestCollectionNotNullPtr", std::move(done)) {}

 private:
  void Run() override {
    // When |flow| goes out of scope, it will call Done() for us.
    FlowToken flow{this};
    operation_collection_.Add(
        std::make_unique<TestOperationNotNullPtr>(std::move(flow)));
  }

  OperationCollection operation_collection_;
};

// See comments on OperationQueue::Drop.
TEST_F(OperationTest, TestQueueNotNullPtr) {
  OperationQueue container;

  bool done_called{false};
  auto op = std::make_unique<TestQueueNotNullPtr>(
      [&done_called] { done_called = true; });

  container.Add(std::move(op));

  // Nothing has run yet because we haven't run the async loop.
  EXPECT_FALSE(done_called);

  // Running the loop we expect done_called to be set to true
  RunLoopUntilIdle();
  EXPECT_TRUE(done_called);
}

// See comments on OperationCollection::Drop.
TEST_F(OperationTest, TestCollectionNotNullPtr) {
  OperationQueue container;

  bool done_called{false};
  auto op = std::make_unique<TestCollectionNotNullPtr>(
      [&done_called] { done_called = true; });

  container.Add(std::move(op));

  // Nothing has run yet because we haven't run the async loop.
  EXPECT_FALSE(done_called);

  // Running the loop we expect done_called to be set to true
  RunLoopUntilIdle();
  EXPECT_TRUE(done_called);
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
  container.Add(WrapFutureAsOperation(__PRETTY_FUNCTION__, on_run, done,
                                      fit::function<void(int)>([&](int result) {
                                        EXPECT_EQ(10, result);
                                        op_did_finish = true;
                                      })));

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
      __PRETTY_FUNCTION__, on_run, done,
      fit::function<void()>([&] { op_did_finish = true; })));

  RunLoopUntilIdle();
  EXPECT_TRUE(op_did_start);
  EXPECT_TRUE(op_did_finish);
}

}  // namespace
}  // namespace modular
