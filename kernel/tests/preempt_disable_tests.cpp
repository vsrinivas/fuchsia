// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/atomic.h>
#include <kernel/event.h>
#include <kernel/sched.h>
#include <kernel/timer.h>
#include <platform.h>
#include <unittest.h>

// Test that preempt_disable is set for timer callbacks and that, in this
// context, preempt_pending will get set by some functions.
static void timer_callback_func(timer_t* timer, zx_time_t now, void* arg) {
    event_t* event = (event_t*)arg;

    // Timer callbacks should be called in interrupt context with
    // preempt_disable set.
    ASSERT(arch_ints_disabled());
    ASSERT(arch_in_int_handler());
    thread_t* thread = get_current_thread();
    ASSERT(thread->preempt_disable);

    // Save and restore the value of preempt_pending so that we can test
    // other functions' behavior with preempt_pending==false.  It is
    // possible that preempt_pending is true now: it might have been set by
    // another timer callback.
    bool old_preempt_pending = thread->preempt_pending;

    // Test that sched_reschedule() sets the preempt_pending flag when
    // preempt_disable is set.
    thread->preempt_pending = false;
    THREAD_LOCK(state);
    sched_reschedule();
    THREAD_UNLOCK(state);
    ASSERT(thread->preempt_pending);

    // Test that thread_preempt_set_pending() sets the preempt_pending
    // flag.
    thread->preempt_pending = false;
    thread_preempt_set_pending();
    ASSERT(thread->preempt_pending);

    // Restore value.
    thread->preempt_pending = old_preempt_pending;

    event_signal(event, true);
}

// Schedule a timer callback and wait for it to complete.  Most of the
// testing is done in the timer callback.
static bool test_in_timer_callback() {
    BEGIN_TEST;

    event_t event;
    event_init(&event, false, 0);

    timer_t timer;
    timer_init(&timer);
    timer_set(&timer, 0, TIMER_SLACK_CENTER, 0, timer_callback_func, &event);

    ASSERT_EQ(event_wait(&event), ZX_OK, "");
    event_destroy(&event);

    END_TEST;
}

static void timer_set_preempt_pending(timer_t* timer, zx_time_t now,
                                      void* arg) {
    auto* timer_ran = reinterpret_cast<fbl::atomic<bool>*>(arg);

    thread_preempt_set_pending();
    timer_ran->store(true);
}

static bool test_outside_interrupt() {
    BEGIN_TEST;

    thread_t* thread = get_current_thread();

    // Test initial conditions.
    ASSERT_EQ(thread->preempt_disable, 0u, "");
    // While preemption is allowed, a preemption should not be pending.
    ASSERT_EQ(thread->preempt_pending, false, "");

    // Test incrementing and decrementing of preempt_disable.
    thread_preempt_disable();
    EXPECT_EQ(thread->preempt_disable, 1u, "");
    thread_preempt_reenable();
    EXPECT_EQ(thread->preempt_disable, 0u, "");

    // Test nesting: multiple increments and decrements.
    thread_preempt_disable();
    thread_preempt_disable();
    EXPECT_EQ(thread->preempt_disable, 2u, "");
    thread_preempt_reenable();
    thread_preempt_reenable();
    EXPECT_EQ(thread->preempt_disable, 0u, "");

    // Test that thread_preempt_reenable() clears preempt_pending.
    thread_preempt_disable();
    thread_reschedule();
    // It should not be possible for an interrupt handler to block or
    // otherwise cause a reschedule before our thread_preempt_reenable().
    EXPECT_EQ(thread->preempt_pending, true, "");
    thread_preempt_reenable();
    EXPECT_EQ(thread->preempt_pending, false, "");

    // It is OK to block while preemption is disabled.  In this case,
    // blocking should clear preempt_pending.
    thread_preempt_disable();
    thread_reschedule();
    EXPECT_EQ(thread->preempt_pending, true, "");
    arch_disable_ints();
    thread_sleep_relative(ZX_MSEC(10));
    // Read preempt_pending with interrupts disabled because otherwise an
    // interrupt handler could set it to true.
    EXPECT_EQ(thread->preempt_pending, false, "");
    arch_enable_ints();
    thread_preempt_reenable();

    // Test that interrupt handlers honor preempt_disable.
    //
    // We test that by setting a timer callback that will set
    // preempt_pending from inside an interrupt handler.  preempt_pending
    // should remain set after the interrupt handler returns.
    //
    // This assumes that timer_set() will run the callback on the same CPU
    // that we invoked it from.  This also assumes that we don't
    // accidentally call any blocking operations that cause our thread to
    // be rescheduled to another CPU.
    thread_preempt_disable();
    fbl::atomic<bool> timer_ran(false);
    timer_t timer;
    timer_init(&timer);
    timer_set(&timer, current_time() + ZX_USEC(100), TIMER_SLACK_CENTER, 0,
              timer_set_preempt_pending, reinterpret_cast<void*>(&timer_ran));
    // Spin until timer_ran is set by the interrupt handler.
    while (!timer_ran.load()) {}
    EXPECT_EQ(thread->preempt_pending, true, "");
    thread_preempt_reenable();

    END_TEST;
}

UNITTEST_START_TESTCASE(preempt_disable_tests)
UNITTEST("test_in_timer_callback", test_in_timer_callback)
UNITTEST("test_outside_interrupt", test_outside_interrupt)
UNITTEST_END_TESTCASE(preempt_disable_tests, "preempt_disable_tests",
                      "preempt_disable_tests");
