// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <lib/zx/event.h>
#include <lib/zx/time.h>
#include <unittest/unittest.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <array>
#include <memory>
#include <utility>

namespace {

// Initializes |wait| to wait on |event| to call |closure| once |trigger| is
// signaled.
void InitWait(async::Wait* wait, fit::closure closure, const zx::event& event,
              zx_signals_t trigger) {
  wait->set_handler([closure = std::move(closure)](async_dispatcher_t*, async::Wait*, zx_status_t,
                                                   const zx_packet_signal_t*) { closure(); });
  wait->set_object(event.get());
  wait->set_trigger(trigger);
}

bool DefaultDispatcherIsSetAndUnset() {
  BEGIN_TEST;

  EXPECT_NULL(async_get_default_dispatcher());
  {
    async::TestLoop loop;
    ;
    EXPECT_EQ(loop.dispatcher(), async_get_default_dispatcher());
  }
  EXPECT_NULL(async_get_default_dispatcher());

  END_TEST;
}

bool FakeClockTimeIsCorrect() {
  BEGIN_TEST;

  async::TestLoop loop;

  EXPECT_EQ(0, loop.Now().get());
  EXPECT_EQ(0, async::Now(loop.dispatcher()).get());

  loop.RunUntilIdle();
  EXPECT_EQ(0, loop.Now().get());
  EXPECT_EQ(0, async::Now(loop.dispatcher()).get());

  loop.RunFor(zx::nsec(1));
  EXPECT_EQ(1, loop.Now().get());
  EXPECT_EQ(1, async::Now(loop.dispatcher()).get());

  loop.RunUntil(zx::time() + zx::nsec(3));
  EXPECT_EQ(3, loop.Now().get());
  EXPECT_EQ(3, async::Now(loop.dispatcher()).get());

  loop.RunFor(zx::nsec(7));
  EXPECT_EQ(10, loop.Now().get());
  EXPECT_EQ(10, async::Now(loop.dispatcher()).get());

  loop.RunUntil(zx::time() + zx::nsec(12));
  EXPECT_EQ(12, loop.Now().get());
  EXPECT_EQ(12, async::Now(loop.dispatcher()).get());

  // t = 12, so nothing should happen in trying to reset the clock to t = 10.
  loop.RunUntil(zx::time() + zx::nsec(10));
  EXPECT_EQ(12, loop.Now().get());
  EXPECT_EQ(12, async::Now(loop.dispatcher()).get());

  END_TEST;
}

bool TasksAreDispatched() {
  BEGIN_TEST;

  async::TestLoop loop;
  bool called = false;
  async::PostDelayedTask(
      loop.dispatcher(), [&called] { called = true; }, zx::sec(2));

  // t = 1: nothing should happen.
  loop.RunFor(zx::sec(1));
  EXPECT_FALSE(called);

  // t = 2: task should be dispatched.
  loop.RunFor(zx::sec(1));
  EXPECT_TRUE(called);

  called = false;
  async::PostTask(loop.dispatcher(), [&called] { called = true; });
  loop.RunUntilIdle();
  EXPECT_TRUE(called);

  END_TEST;
}

bool SameDeadlinesDispatchInPostingOrder() {
  BEGIN_TEST;

  async::TestLoop loop;
  bool calledA = false;
  bool calledB = false;

  async::PostTask(loop.dispatcher(), [&] {
    EXPECT_FALSE(calledB);
    calledA = true;
  });
  async::PostTask(loop.dispatcher(), [&] {
    EXPECT_TRUE(calledA);
    calledB = true;
  });

  loop.RunUntilIdle();
  EXPECT_TRUE(calledA);
  EXPECT_TRUE(calledB);

  calledA = false;
  calledB = false;
  async::PostDelayedTask(
      loop.dispatcher(),
      [&] {
        EXPECT_FALSE(calledB);
        calledA = true;
      },
      zx::sec(5));
  async::PostDelayedTask(
      loop.dispatcher(),
      [&] {
        EXPECT_TRUE(calledA);
        calledB = true;
      },
      zx::sec(5));

  loop.RunFor(zx::sec(5));
  EXPECT_TRUE(calledA);
  EXPECT_TRUE(calledB);

  END_TEST;
}

// Test tasks that post tasks.
bool NestedTasksAreDispatched() {
  BEGIN_TEST;

  async::TestLoop loop;
  bool called = false;

  async::PostTask(loop.dispatcher(), [&] {
    async::PostDelayedTask(
        loop.dispatcher(),
        [&] {
          async::PostDelayedTask(
              loop.dispatcher(), [&] { called = true; }, zx::min(25));
        },
        zx::min(35));
  });

  loop.RunFor(zx::hour(1));
  EXPECT_TRUE(called);

  END_TEST;
}

bool TimeIsCorrectWhileDispatching() {
  BEGIN_TEST;

  async::TestLoop loop;
  bool called = false;

  async::PostTask(loop.dispatcher(), [&] {
    EXPECT_EQ(0, loop.Now().get());

    async::PostDelayedTask(
        loop.dispatcher(),
        [&] {
          EXPECT_EQ(10, loop.Now().get());
          async::PostDelayedTask(
              loop.dispatcher(),
              [&] {
                EXPECT_EQ(15, loop.Now().get());
                async::PostTask(loop.dispatcher(), [&] {
                  EXPECT_EQ(15, loop.Now().get());
                  called = true;
                });
              },
              zx::nsec(5));
        },
        zx::nsec(10));
  });

  loop.RunFor(zx::nsec(15));
  EXPECT_TRUE(called);

  END_TEST;
}

bool TasksAreCanceled() {
  BEGIN_TEST;

  async::TestLoop loop;
  bool calledA = false;
  bool calledB = false;
  bool calledC = false;

  async::TaskClosure taskA([&calledA] { calledA = true; });
  async::TaskClosure taskB([&calledB] { calledB = true; });
  async::TaskClosure taskC([&calledC] { calledC = true; });

  ASSERT_EQ(ZX_OK, taskA.Post(loop.dispatcher()));
  ASSERT_EQ(ZX_OK, taskB.Post(loop.dispatcher()));
  ASSERT_EQ(ZX_OK, taskC.Post(loop.dispatcher()));

  ASSERT_EQ(ZX_OK, taskA.Cancel());
  ASSERT_EQ(ZX_OK, taskC.Cancel());

  loop.RunUntilIdle();

  EXPECT_FALSE(calledA);
  EXPECT_TRUE(calledB);
  EXPECT_FALSE(calledC);

  END_TEST;
}

bool TimeIsAdvanced() {
  BEGIN_TEST;
  async::TestLoop loop;

  bool called = false;
  async::TaskClosure task([&called] { called = true; });
  auto time1 = async::Now(loop.dispatcher());

  ASSERT_EQ(ZX_OK, task.PostDelayed(loop.dispatcher(), zx::duration(1)));

  loop.RunUntilIdle();

  EXPECT_FALSE(called);
  EXPECT_EQ(time1.get(), async::Now(loop.dispatcher()).get());

  loop.AdvanceTimeByEpsilon();

  auto time2 = async::Now(loop.dispatcher());

  EXPECT_FALSE(called);
  EXPECT_GT(time2.get(), time1.get());

  loop.RunUntilIdle();

  EXPECT_TRUE(called);
  EXPECT_EQ(time2.get(), async::Now(loop.dispatcher()).get());

  END_TEST;
}

bool WaitsAreDispatched() {
  BEGIN_TEST;

  async::TestLoop loop;
  async::Wait wait;
  zx::event event;
  bool called = false;

  ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));
  InitWait(
      &wait, [&called] { called = true; }, event, ZX_USER_SIGNAL_0);
  ASSERT_EQ(ZX_OK, wait.Begin(loop.dispatcher()));

  // |wait| has not yet been triggered.
  loop.RunUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_1));

  // |wait| will only be triggered by |ZX_USER_SIGNAL_0|.
  loop.RunUntilIdle();
  EXPECT_FALSE(called);

  ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

  loop.RunUntilIdle();
  EXPECT_TRUE(called);

  END_TEST;
}

// Test waits that trigger waits.
bool NestedWaitsAreDispatched() {
  BEGIN_TEST;

  async::TestLoop loop;
  zx::event event;
  async::Wait waitA;
  async::Wait waitB;
  async::Wait waitC;
  bool calledA = false;
  bool calledB = false;
  bool calledC = false;

  ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));
  InitWait(
      &waitA,
      [&] {
        InitWait(
            &waitB,
            [&] {
              InitWait(
                  &waitC, [&] { calledC = true; }, event, ZX_USER_SIGNAL_2);
              waitC.Begin(loop.dispatcher());
              calledB = true;
            },
            event, ZX_USER_SIGNAL_1);
        waitB.Begin(loop.dispatcher());
        calledA = true;
      },
      event, ZX_USER_SIGNAL_0);

  ASSERT_EQ(ZX_OK, waitA.Begin(loop.dispatcher()));

  loop.RunUntilIdle();
  EXPECT_FALSE(calledA);
  EXPECT_FALSE(calledB);
  EXPECT_FALSE(calledC);

  ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

  loop.RunUntilIdle();
  EXPECT_TRUE(calledA);
  EXPECT_FALSE(calledB);
  EXPECT_FALSE(calledC);

  ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_1));

  loop.RunUntilIdle();
  EXPECT_TRUE(calledA);
  EXPECT_TRUE(calledB);
  EXPECT_FALSE(calledC);

  ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_2));

  loop.RunUntilIdle();
  EXPECT_TRUE(calledA);
  EXPECT_TRUE(calledB);
  EXPECT_TRUE(calledC);

  END_TEST;
}

bool WaitsAreCanceled() {
  BEGIN_TEST;

  async::TestLoop loop;
  zx::event event;
  async::Wait waitA;
  async::Wait waitB;
  async::Wait waitC;
  bool calledA = false;
  bool calledB = false;
  bool calledC = false;

  ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));

  InitWait(
      &waitA, [&calledA] { calledA = true; }, event, ZX_USER_SIGNAL_0);
  InitWait(
      &waitB, [&calledB] { calledB = true; }, event, ZX_USER_SIGNAL_0);
  InitWait(
      &waitC, [&calledC] { calledC = true; }, event, ZX_USER_SIGNAL_0);

  ASSERT_EQ(ZX_OK, waitA.Begin(loop.dispatcher()));
  ASSERT_EQ(ZX_OK, waitB.Begin(loop.dispatcher()));
  ASSERT_EQ(ZX_OK, waitC.Begin(loop.dispatcher()));

  ASSERT_EQ(ZX_OK, waitA.Cancel());
  ASSERT_EQ(ZX_OK, waitC.Cancel());
  ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

  loop.RunUntilIdle();
  EXPECT_FALSE(calledA);
  EXPECT_TRUE(calledB);
  EXPECT_FALSE(calledC);

  END_TEST;
}

// Test a task that begins a wait to post a task.
bool NestedTasksAndWaitsAreDispatched() {
  BEGIN_TEST;

  async::TestLoop loop;
  zx::event event;
  async::Wait wait;
  bool wait_begun = false;
  bool wait_dispatched = false;
  bool inner_task_dispatched = false;

  ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));
  InitWait(
      &wait,
      [&] {
        async::PostDelayedTask(
            loop.dispatcher(), [&] { inner_task_dispatched = true; }, zx::min(2));
        wait_dispatched = true;
      },
      event, ZX_USER_SIGNAL_0);
  async::PostDelayedTask(
      loop.dispatcher(),
      [&] {
        wait.Begin(loop.dispatcher());
        wait_begun = true;
      },
      zx::min(3));

  loop.RunFor(zx::min(3));
  EXPECT_TRUE(wait_begun);
  EXPECT_FALSE(wait_dispatched);
  EXPECT_FALSE(inner_task_dispatched);

  ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

  loop.RunUntilIdle();
  EXPECT_TRUE(wait_begun);
  EXPECT_TRUE(wait_dispatched);
  EXPECT_FALSE(inner_task_dispatched);

  loop.RunFor(zx::min(2));
  EXPECT_TRUE(wait_begun);
  EXPECT_TRUE(wait_dispatched);
  EXPECT_TRUE(inner_task_dispatched);

  END_TEST;
}

bool DefaultDispatcherIsCurrentLoop() {
  BEGIN_TEST;

  async::TestLoop loop;
  auto subloop = loop.StartNewLoop();
  bool main_loop_task_run = false;
  async_dispatcher_t* main_loop_task_dispatcher = nullptr;
  bool sub_loop_task_run = false;
  async_dispatcher_t* sub_loop_task_dispatcher = nullptr;

  async::PostTask(loop.dispatcher(), [&] {
    main_loop_task_run = true;
    main_loop_task_dispatcher = async_get_default_dispatcher();
  });

  async::PostTask(subloop->dispatcher(), [&] {
    sub_loop_task_run = true;
    sub_loop_task_dispatcher = async_get_default_dispatcher();
  });

  loop.RunUntilIdle();
  EXPECT_TRUE(main_loop_task_run);
  EXPECT_EQ(main_loop_task_dispatcher, loop.dispatcher());
  EXPECT_TRUE(sub_loop_task_run);
  EXPECT_EQ(sub_loop_task_dispatcher, subloop->dispatcher());

  END_TEST;
}

bool HugeAmountOfTaskAreDispatched() {
  BEGIN_TEST;

  constexpr size_t kPostCount = 128 * 1024;
  async::TestLoop loop;
  zx::event event;
  ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));

  size_t called_count = 0;
  size_t wait_count = 0;
  // Creating the array on the heap as its size is greater than the available
  // stack.
  auto waits_ptr = std::make_unique<std::array<async::Wait, kPostCount>>();
  auto& waits = *waits_ptr;

  for (size_t i = 0; i < kPostCount; ++i) {
    InitWait(
        &waits[i], [&] { wait_count++; }, event, ZX_USER_SIGNAL_0);
    ASSERT_EQ(ZX_OK, waits[i].Begin(loop.dispatcher()));
  }
  ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));
  for (size_t i = 0; i < kPostCount; ++i) {
    async::PostTask(loop.dispatcher(), [&] { called_count++; });
  }

  loop.RunUntilIdle();

  EXPECT_EQ(kPostCount, called_count);
  EXPECT_EQ(kPostCount, wait_count);
  END_TEST;
}

bool TasksAreDispatchedOnManyLoops() {
  BEGIN_TEST;

  async::TestLoop loop;
  auto loopA = loop.StartNewLoop();
  auto loopB = loop.StartNewLoop();
  auto loopC = loop.StartNewLoop();

  bool called = false;
  bool calledA = false;
  bool calledB = false;
  bool calledC = false;
  async::TaskClosure taskC([&calledC] { calledC = true; });

  async::PostTask(loopB->dispatcher(), [&calledB] { calledB = true; });
  async::PostDelayedTask(
      loop.dispatcher(), [&called] { called = true; }, zx::sec(1));
  ASSERT_EQ(ZX_OK, taskC.PostDelayed(loopC->dispatcher(), zx::sec(1)));
  async::PostDelayedTask(
      loopA->dispatcher(), [&calledA] { calledA = true; }, zx::sec(2));

  loop.RunUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_FALSE(calledA);
  EXPECT_TRUE(calledB);
  EXPECT_FALSE(calledC);

  taskC.Cancel();
  loop.RunFor(zx::sec(1));
  EXPECT_TRUE(called);
  EXPECT_FALSE(calledA);
  EXPECT_TRUE(calledB);
  EXPECT_FALSE(calledC);

  loop.RunFor(zx::sec(1));
  EXPECT_TRUE(called);
  EXPECT_TRUE(calledA);
  EXPECT_TRUE(calledB);
  EXPECT_FALSE(calledC);

  END_TEST;
}

bool WaitsAreDispatchedOnManyLoops() {
  BEGIN_TEST;

  async::TestLoop loop;
  auto loopA = loop.StartNewLoop();
  auto loopB = loop.StartNewLoop();
  auto loopC = loop.StartNewLoop();
  async::Wait wait;
  async::Wait waitA;
  async::Wait waitB;
  async::Wait waitC;
  bool called = false;
  bool calledA = false;
  bool calledB = false;
  bool calledC = false;
  zx::event event;

  ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));

  InitWait(
      &wait, [&called] { called = true; }, event, ZX_USER_SIGNAL_0);
  InitWait(
      &waitA, [&calledA] { calledA = true; }, event, ZX_USER_SIGNAL_0);
  InitWait(
      &waitB, [&calledB] { calledB = true; }, event, ZX_USER_SIGNAL_0);
  InitWait(
      &waitC, [&calledC] { calledC = true; }, event, ZX_USER_SIGNAL_0);

  ASSERT_EQ(ZX_OK, wait.Begin(loop.dispatcher()));
  ASSERT_EQ(ZX_OK, waitA.Begin(loopA->dispatcher()));
  ASSERT_EQ(ZX_OK, waitB.Begin(loopB->dispatcher()));
  ASSERT_EQ(ZX_OK, waitC.Begin(loopC->dispatcher()));

  ASSERT_EQ(ZX_OK, waitB.Cancel());
  ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

  loop.RunUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_TRUE(calledA);
  EXPECT_FALSE(calledB);
  EXPECT_TRUE(calledC);

  END_TEST;
}

// Populates |order| with the order in which two tasks and two waits on four
// loops were dispatched, given a |loop|.
bool DetermineDispatchOrder(std::unique_ptr<async::TestLoop> loop, int (*order)[4]) {
  BEGIN_HELPER;

  auto loopA = loop->StartNewLoop();
  auto loopB = loop->StartNewLoop();
  auto loopC = loop->StartNewLoop();
  async::Wait wait;
  async::Wait waitB;
  zx::event event;
  int i = 0;

  ASSERT_EQ(ZX_OK, zx::event::create(0u, &event));

  InitWait(
      &wait, [&] { (*order)[0] = ++i; }, event, ZX_USER_SIGNAL_0);
  async::PostTask(loopA->dispatcher(), [&] { (*order)[1] = ++i; });
  InitWait(
      &waitB, [&] { (*order)[2] = ++i; }, event, ZX_USER_SIGNAL_0);
  async::PostTask(loopC->dispatcher(), [&] { (*order)[3] = ++i; });

  ASSERT_EQ(ZX_OK, wait.Begin(loop->dispatcher()));
  ASSERT_EQ(ZX_OK, waitB.Begin(loopB->dispatcher()));
  ASSERT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));

  loop->RunUntilIdle();

  EXPECT_EQ(4, i);
  EXPECT_NE(0, (*order)[0]);
  EXPECT_NE(0, (*order)[1]);
  EXPECT_NE(0, (*order)[2]);
  EXPECT_NE(0, (*order)[3]);

  END_HELPER;
}

bool SeedTestLoopWithEnv(uint32_t random_seed, std::unique_ptr<async::TestLoop>* loop) {
  BEGIN_HELPER;

  char buf[12];
  snprintf(buf, sizeof(buf), "%u", random_seed);
  EXPECT_EQ(0, setenv("TEST_LOOP_RANDOM_SEED", buf, 1));
  *loop = std::make_unique<async::TestLoop>();
  EXPECT_EQ(0, unsetenv("TEST_LOOP_RANDOM_SEED"));

  END_HELPER;
}

bool DispatchOrderIsDeterministicFor(uint32_t random_seed) {
  BEGIN_HELPER;

  int expected_order[4] = {0, 0, 0, 0};
  std::unique_ptr<async::TestLoop> loop;

  EXPECT_TRUE(SeedTestLoopWithEnv(random_seed, &loop));
  EXPECT_TRUE(DetermineDispatchOrder(std::move(loop), &expected_order));

  for (int i = 0; i < 5; ++i) {
    for (int j = 0; j < 2; j++) {
      int actual_order[4] = {0, 0, 0, 0};
      if (j == 0) {
        EXPECT_TRUE(SeedTestLoopWithEnv(random_seed, &loop));
      } else {
        loop = std::make_unique<async::TestLoop>(random_seed);
      }
      EXPECT_TRUE(DetermineDispatchOrder(std::move(loop), &actual_order));
      EXPECT_EQ(expected_order[0], actual_order[0]);
      EXPECT_EQ(expected_order[1], actual_order[1]);
      EXPECT_EQ(expected_order[2], actual_order[2]);
      EXPECT_EQ(expected_order[3], actual_order[3]);
    }
  }

  END_HELPER;
}

bool DispatchOrderIsDeterministic() {
  BEGIN_TEST;

  EXPECT_TRUE(DispatchOrderIsDeterministicFor(1));
  EXPECT_TRUE(DispatchOrderIsDeterministicFor(43));
  EXPECT_TRUE(DispatchOrderIsDeterministicFor(893));
  EXPECT_TRUE(DispatchOrderIsDeterministicFor(39408));
  EXPECT_TRUE(DispatchOrderIsDeterministicFor(844018));
  EXPECT_TRUE(DispatchOrderIsDeterministicFor(83018299));
  EXPECT_TRUE(DispatchOrderIsDeterministicFor(3213));
  EXPECT_TRUE(DispatchOrderIsDeterministicFor(139133113));
  EXPECT_TRUE(DispatchOrderIsDeterministicFor(1323234373));

  END_TEST;
}

// Test that non-async-dispatcher loops run fine.
struct ExternalLoop : async_test_subloop_t {
  ExternalLoop() { ops = &kOps; }

  // The loop keeps a state, that is repeatedly incremented.
  // 0: advance to 1
  // 1: wait until |time_ >= kState1Deadline|, advance to 2
  // 2: advance to 3
  // 3: blocked, needs to be manually advanced
  // 4: advance to 5
  // 5: done, do not increment
  // 6: finalized
  int state_ = 0;

  // The current time, according to the TestLoop.
  zx_time_t time_ = ZX_TIME_INFINITE_PAST;

  constexpr static zx_time_t kState1Deadline = 1000;
  constexpr static int kStateFinalized = 6;

  // Returns the minimum time for the next transition starting from |state|.
  // If |ZX_TIME_INFINITE| is returned, the state should not be advanced.
  static zx_time_t NextTransitionTime(int state) {
    switch (state) {
      case 0:
      case 2:
      case 4:
        // Advance immediately.
        return ZX_TIME_INFINITE_PAST;
      case 1:
        return kState1Deadline;
      case 3:
      case 5:
        return ZX_TIME_INFINITE;
      default:
        ZX_ASSERT(false);
    }
  }

  static void advance_time_to(async_test_subloop_t* self_generic, zx_time_t time) {
    ExternalLoop* self = static_cast<ExternalLoop*>(self_generic);
    ZX_ASSERT(self->state_ != kStateFinalized);
    static_cast<ExternalLoop*>(self)->time_ = time;
  }

  static uint8_t dispatch_next_due_message(async_test_subloop_t* self_generic) {
    ExternalLoop* self = static_cast<ExternalLoop*>(self_generic);
    ZX_ASSERT(self->state_ != kStateFinalized);
    zx_time_t transition_time = NextTransitionTime(self->state_);
    if (transition_time != ZX_TIME_INFINITE && transition_time <= self->time_) {
      self->state_++;
      return true;
    } else {
      return false;
    }
  }

  static uint8_t has_pending_work(async_test_subloop_t* self_generic) {
    ExternalLoop* self = static_cast<ExternalLoop*>(self_generic);
    ZX_ASSERT(self->state_ != kStateFinalized);
    zx_time_t transition_time = NextTransitionTime(self->state_);
    return (transition_time != ZX_TIME_INFINITE && transition_time <= self->time_);
  }

  static zx_time_t get_next_task_due_time(async_test_subloop_t* self_generic) {
    ExternalLoop* self = static_cast<ExternalLoop*>(self_generic);
    ZX_ASSERT(self->state_ != kStateFinalized);
    return NextTransitionTime(self->state_);
  }

  static void finalize(async_test_subloop_t* self_generic) {
    ExternalLoop* self = static_cast<ExternalLoop*>(self_generic);
    ZX_ASSERT(self->state_ != kStateFinalized);
    self->state_ = kStateFinalized;
  }

  constexpr static async_test_subloop_ops_t kOps = {advance_time_to, dispatch_next_due_message,
                                                    has_pending_work, get_next_task_due_time,
                                                    finalize};
};

bool ExternalLoopIsRunAndFinalized() {
  BEGIN_TEST;

  auto loop = std::make_unique<async::TestLoop>();
  ExternalLoop subloop;
  auto token = loop->RegisterLoop(&subloop);
  EXPECT_TRUE(loop->RunUntilIdle());
  EXPECT_EQ(1, subloop.state_);

  EXPECT_TRUE(loop->RunUntil(zx::time(subloop.kState1Deadline)));
  EXPECT_EQ(3, subloop.state_);
  EXPECT_LE(subloop.kState1Deadline, subloop.time_);

  subloop.state_ = 4;
  EXPECT_TRUE(loop->RunUntilIdle());
  EXPECT_EQ(5, subloop.state_);

  token.reset();
  EXPECT_EQ(subloop.kStateFinalized, subloop.state_);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(SingleLoopTests)
RUN_TEST(DefaultDispatcherIsSetAndUnset)
RUN_TEST(FakeClockTimeIsCorrect)
RUN_TEST(HugeAmountOfTaskAreDispatched)
RUN_TEST(TasksAreDispatched)
RUN_TEST(SameDeadlinesDispatchInPostingOrder)
RUN_TEST(NestedTasksAreDispatched)
RUN_TEST(DefaultDispatcherIsCurrentLoop)
RUN_TEST(TimeIsCorrectWhileDispatching)
RUN_TEST(TasksAreCanceled)
RUN_TEST(TimeIsAdvanced)
RUN_TEST(WaitsAreDispatched)
RUN_TEST(NestedWaitsAreDispatched)
RUN_TEST(WaitsAreCanceled)
RUN_TEST(NestedTasksAndWaitsAreDispatched)
RUN_TEST(ExternalLoopIsRunAndFinalized)
END_TEST_CASE(SingleLoopTests)

BEGIN_TEST_CASE(MultiLoopTests)
RUN_TEST(TasksAreDispatchedOnManyLoops)
RUN_TEST(WaitsAreDispatchedOnManyLoops)
RUN_TEST(DispatchOrderIsDeterministic)
END_TEST_CASE(MultiLoopTests)
