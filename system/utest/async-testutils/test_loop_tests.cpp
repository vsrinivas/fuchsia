// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-testutils/test_loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>

#include <lib/zx/event.h>
#include <lib/zx/time.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace {

// Initializes |task| to update |var| to point to |value| when it fires.
void InitVariableUpdateTask(async::TaskClosure* task, int* var, int value) {
    task->set_handler([var, value]() {
        *var = value;
    });
}

// Initializes |wait| to updates |var| to point to |value| once |ZX_USER_SIGNAL_0| is
// seen.
void InitVariableUpdateWait(async::Wait* wait, int* var, int value, zx::event* event) {
    wait->set_object(event->get());
    wait->set_trigger(ZX_USER_SIGNAL_0);
    wait->set_handler([var, value](async_t*, async::Wait*,
                                   zx_status_t status, const zx_packet_signal_t*) {
        if (status == ZX_OK) {
            *var = value;
        }
    });
}

bool get_default_test() {
    BEGIN_TEST;

    EXPECT_NULL(async_get_default());

    async::TestLoop* loop = new async::TestLoop();
    EXPECT_EQ(loop->async(), async_get_default());

    delete loop;
    EXPECT_NULL(async_get_default());

    END_TEST;
}

bool fake_clock_test() {
    async::TestLoop loop;

    BEGIN_TEST;

    EXPECT_EQ(zx::time(0).get(), loop.Now().get());
    EXPECT_EQ(zx::time(0).get(), async_now(loop.async()));

    loop.AdvanceTimeBy(zx::sec(1));
    EXPECT_EQ(zx::sec(1).get(), loop.Now().get());

    loop.AdvanceTimeBy(zx::sec(2));
    EXPECT_EQ(zx::sec(3).get(), loop.Now().get());
    END_TEST;
}

bool simple_task_posting_test() {
    async::TestLoop loop;
    async_t* async = loop.async();
    int var = 0;
    BEGIN_TEST;

    // |taskA|: updates |var| to 1 with a deadline of t = 2.
    async::TaskClosure taskA;
    InitVariableUpdateTask(&taskA, &var, 1);
    EXPECT_EQ(ZX_OK, taskA.PostForTime(async, zx::time(0) + zx::sec(2)));
    loop.RunUntilIdle();
    // t = 1: nothing should happen, as |taskA| has a deadline of 1.
    EXPECT_EQ(0, var);

    // t = 1: nothing should happen, as |taskA| has a deadline of 2.
    loop.AdvanceTimeBy(zx::sec(1));
    loop.RunUntilIdle();
    EXPECT_EQ(0, var);

    // t = 2: |taskA| should have updated |var| to 1.
    loop.AdvanceTimeBy(zx::sec(1));
    loop.RunUntilIdle();
    EXPECT_EQ(1, var);

    END_TEST;
}

bool task_with_same_deadlines_test() {
    async::TestLoop loop;
    async_t* async = loop.async();
    int var = 0;
    BEGIN_TEST;

    // |taskA|: updates |var| to 1 with a deadline of t = 3.
    // |taskB|: updates |var| to 2 with a deadline of t = 3.
    async::TaskClosure taskA;
    InitVariableUpdateTask(&taskA, &var, 1);
    EXPECT_EQ(ZX_OK, taskA.PostForTime(async, zx::time(0) + zx::sec(3)));
    async::TaskClosure taskB;
    InitVariableUpdateTask(&taskB, &var, 2);
    EXPECT_EQ(ZX_OK, taskB.PostForTime(async, zx::time(0) + zx::sec(3)));

    // t = 3: Since |taskB| was posted after |taskA|, it's handler was called
    //  after |taskA|'s.'
    loop.AdvanceTimeBy(zx::sec(3));
    loop.RunUntilIdle();
    EXPECT_EQ(2, var);

    END_TEST;
}

// Test tasks that post tasks.
bool compounded_task_posting_test() {
    async::TestLoop loop;
    async_t* async = loop.async();
    int var = 0;

    BEGIN_TEST;
    // |taskA|: updates |var| to 1 and posts |taskB| at t = 1.
    // |taskB|: updates |var| to 2 at t = 3.
    async::TaskClosure taskB;
    InitVariableUpdateTask(&taskB, &var, 2);

    async::TaskClosure taskA;
    taskA.set_handler([&async, &taskB, &var]() {
        var = 1;
        EXPECT_EQ(ZX_OK, taskB.PostForTime(async, zx::time(0) + zx::sec(2)));
    });

    EXPECT_EQ(ZX_OK, taskA.PostForTime(async, zx::time(1)));
    EXPECT_EQ(0, var);

    // t = 1: |taskA| should have updated |var| to 1.
    loop.AdvanceTimeBy(zx::sec(1));
    loop.RunUntilIdle();
    EXPECT_EQ(1, var);

    // t = 2: |taskB| should have updated |var| to 2.
    loop.AdvanceTimeBy(zx::sec(1));
    loop.RunUntilIdle();
    EXPECT_EQ(2, var);

    END_TEST;
}

bool task_canceling_test() {
    async::TestLoop loop;
    async_t* async = loop.async();
    int var = 0;
    async::TaskClosure taskA;
    InitVariableUpdateTask(&taskA, &var, 2);
    async::TaskClosure taskB;
    InitVariableUpdateTask(&taskB, &var, 1);
    async::TaskClosure taskC;
    InitVariableUpdateTask(&taskC, &var, 3);
    BEGIN_TEST;

    EXPECT_EQ(ZX_OK, taskA.PostForTime(async, zx::time(0) + zx::sec(1)));
    EXPECT_EQ(ZX_OK, taskB.PostForTime(async, zx::time(0) + zx::sec(2)));
    EXPECT_EQ(ZX_OK, taskC.PostForTime(async, zx::time(0) + zx::sec(3)));

    loop.AdvanceTimeBy(zx::sec(2));
    EXPECT_EQ(ZX_OK, taskB.Cancel());
    loop.RunUntilIdle();

    // t = 2; both |taskA| and |taskB| would be due, but since |taskB| was cancelled
    // only |taskA|'s handler was called: |var| should be 2.
    // unchanged.
    EXPECT_EQ(2, var);

    EXPECT_EQ(ZX_OK, taskC.Cancel());
    loop.AdvanceTimeBy(zx::sec(1));
    loop.RunUntilIdle();

    // t = 3: |taskC| was cancelled, so |var| should remain at 2.
    EXPECT_EQ(2, var);

    END_TEST;
}

bool simple_wait_posting_test() {
    async::TestLoop loop;
    async_t* async = loop.async();
    int var = 0;
    zx::event event;
    async::Wait wait;
    BEGIN_TEST;

    EXPECT_EQ(ZX_OK, zx::event::create(0u, &event));
    InitVariableUpdateWait(&wait, &var, 1, &event);

    EXPECT_EQ(ZX_OK, wait.Begin(async));
    loop.RunUntilIdle();
    EXPECT_EQ(0, var);

    // |wait| will only be triggered by |ZX_USER_SIGNAL_1|.
    EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_1));
    loop.RunUntilIdle();
    EXPECT_EQ(0, var);

    // With the correct signal, |var| should be updated to 1.
    EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));
    loop.RunUntilIdle();
    EXPECT_EQ(1, var);

    END_TEST;
}

// Test waits that trigger waits.
bool compounded_wait_posting_test() {
    async::TestLoop loop;
    async_t* async = loop.async();
    int var = 0;
    zx::event event;

    BEGIN_TEST;
    EXPECT_EQ(ZX_OK, zx::event::create(0u, &event));

    // |waitA|: updates |var| to 1 and begins |waitB| on |ZX_USER_SIGNAL_1|.
    // |waitB|: updates |var| to 2 on |ZX_USER_SIGNAL_0|.
    async::Wait waitB;
    InitVariableUpdateWait(&waitB, &var, 2, &event);

    async::Wait waitA;
    waitA.set_object(event.get());
    waitA.set_trigger(ZX_USER_SIGNAL_1);
    waitA.set_handler([&waitB, &var](async_t* async, async::Wait* wait,
                                     zx_status_t status, const zx_packet_signal_t*) {
        if (status == ZX_OK) {
            var = 1;
            EXPECT_EQ(ZX_OK, waitB.Begin(async));
        }
    });
    EXPECT_EQ(ZX_OK, waitA.Begin(async));
    EXPECT_EQ(0, var);

    // |waitA| should trigger.
    EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_1));
    loop.RunUntilIdle();
    EXPECT_EQ(1, var);

    // |waitB| should have begun by now and trigger.
    EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));
    loop.RunUntilIdle();
    EXPECT_EQ(2, var);

    END_TEST;
}

bool wait_canceling_test() {
    async::TestLoop loop;
    async_t* async = loop.async();
    int var = 0;
    zx::event eventA;
    zx::event eventB;
    zx::event eventC;
    async::Wait waitA;
    async::Wait waitB;
    async::Wait waitC;
    BEGIN_TEST;

    EXPECT_EQ(ZX_OK, zx::event::create(0u, &eventA));
    EXPECT_EQ(ZX_OK, zx::event::create(0u, &eventB));
    EXPECT_EQ(ZX_OK, zx::event::create(0u, &eventC));

    InitVariableUpdateWait(&waitA, &var, 1, &eventA);
    InitVariableUpdateWait(&waitB, &var, 2, &eventB);
    InitVariableUpdateWait(&waitC, &var, 3, &eventC);

    EXPECT_EQ(ZX_OK, waitA.Begin(async));
    EXPECT_EQ(ZX_OK, waitB.Begin(async));
    EXPECT_EQ(ZX_OK, waitC.Begin(async));

    EXPECT_EQ(ZX_OK, waitB.Cancel());

    // Have |eventA| and |eventB| fire: |waitB| was canceled, so only |waitA|'s
    // shoud have been called: |var| should be 1.
    EXPECT_EQ(ZX_OK, eventA.signal(0u, ZX_USER_SIGNAL_0));
    EXPECT_EQ(ZX_OK, eventB.signal(0u, ZX_USER_SIGNAL_0));
    loop.RunUntilIdle();
    EXPECT_EQ(1, var);

    // If |eventC| fires before the canceling of |waitC|, |waitC|'s handler
    // should still not be called.
    EXPECT_EQ(ZX_OK, eventC.signal(0u, ZX_USER_SIGNAL_0));
    EXPECT_EQ(ZX_OK, waitC.Cancel());
    loop.RunUntilIdle();
    EXPECT_EQ(1, var);

    END_TEST;
}

// Test a task that begins a wait to post a task.
bool mixed_task_and_wait_posting_test() {

    async::TestLoop loop;
    async_t* async = loop.async();
    int var = 0;
    zx::event event;
    BEGIN_TEST;

    EXPECT_EQ(ZX_OK, zx::event::create(0u, &event));

    // |taskA|: update var to 1 and begin |waitB| with a deadline of t = 1;
    // |waitB|: update var to 2 and post |taskC| on seeing |ZX_USER_SIGNAL_0|;
    // |taskC|: update var to 3 with a deadline of t = 3;
    async::TaskClosure taskC;
    InitVariableUpdateTask(&taskC, &var, 3);

    async::Wait waitB;
    waitB.set_object(event.get());
    waitB.set_trigger(ZX_USER_SIGNAL_0);
    waitB.set_handler([&taskC, &var](async_t* async, async::Wait* wait,
                                     zx_status_t status, const zx_packet_signal_t*) {
        if (status == ZX_OK) {
            var = 2;
            EXPECT_EQ(ZX_OK, taskC.PostForTime(async, zx::time(0) + zx::sec(2)));
        }
    });

    async::TaskClosure taskA;
    taskA.set_handler([&async, &waitB, &var]() {
        var = 1;
        EXPECT_EQ(ZX_OK, waitB.Begin(async));
    });
    EXPECT_EQ(ZX_OK, taskA.PostForTime(async, zx::time(1)));
    EXPECT_EQ(0, var);

    // t = 1: |taskA| should have updated |var| to 1.
    loop.AdvanceTimeBy(zx::sec(1));
    loop.RunUntilIdle();
    EXPECT_EQ(1, var);

    // By now, |waitB| should have begun; on signal, |var| should be updated to 2.
    EXPECT_EQ(ZX_OK, event.signal(0u, ZX_USER_SIGNAL_0));
    loop.RunUntilIdle();
    EXPECT_EQ(2, var);

    // t = 3: |taskC| should have updated |var| to 2.
    loop.AdvanceTimeBy(zx::sec(1));
    loop.RunUntilIdle();
    EXPECT_EQ(3, var);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(test_loop_tests)
RUN_TEST(get_default_test)
RUN_TEST(fake_clock_test)
RUN_TEST(simple_task_posting_test)
RUN_TEST(task_with_same_deadlines_test)
RUN_TEST(compounded_task_posting_test)
RUN_TEST(task_canceling_test)
RUN_TEST(simple_wait_posting_test)
RUN_TEST(compounded_wait_posting_test)
RUN_TEST(wait_canceling_test)
RUN_TEST(mixed_task_and_wait_posting_test)
END_TEST_CASE(test_loop_tests)
