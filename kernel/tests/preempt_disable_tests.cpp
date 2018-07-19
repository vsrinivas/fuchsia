// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/atomic.h>
#include <kernel/event.h>
#include <kernel/interrupt.h>
#include <kernel/sched.h>
#include <kernel/thread_lock.h>
#include <kernel/timer.h>
#include <platform.h>
#include <lib/unittest/unittest.h>

// Test that preempt_disable is set for timer callbacks and that, in this
// context, preempt_pending will get set by some functions.
static void timer_callback_func(timer_t* timer, zx_time_t now, void* arg) {
    event_t* event = (event_t*)arg;

    // Timer callbacks should be called in interrupt context with
    // preempt_disable set.
    ASSERT(arch_ints_disabled());
    ASSERT(arch_in_int_handler());
    thread_t* thread = get_current_thread();
    ASSERT(thread_preempt_disable_count() > 0);

    // Save and restore the value of preempt_pending so that we can test
    // other functions' behavior with preempt_pending==false.  It is
    // possible that preempt_pending is true now: it might have been set by
    // another timer callback.
    bool old_preempt_pending = thread->preempt_pending;

    // Test that sched_reschedule() sets the preempt_pending flag when
    // preempt_disable is set.
    thread->preempt_pending = false;
    {
        Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
        sched_reschedule();
    }
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

// Test incrementing and decrementing the preempt_disable and
// resched_disable counts.
static bool test_inc_dec_disable_counts() {
    BEGIN_TEST;

    thread_t* thread = get_current_thread();

    // Test initial conditions.
    ASSERT_EQ(thread_preempt_disable_count(), 0u, "");
    ASSERT_EQ(thread_resched_disable_count(), 0u, "");
    // While preemption is allowed, a preemption should not be pending.
    ASSERT_EQ(thread->preempt_pending, false, "");

    // Test incrementing and decrementing of preempt_disable.
    thread_preempt_disable();
    EXPECT_EQ(thread_preempt_disable_count(), 1u, "");
    thread_preempt_reenable();
    EXPECT_EQ(thread_preempt_disable_count(), 0u, "");

    // Test incrementing and decrementing of resched_disable.
    thread_resched_disable();
    EXPECT_EQ(thread_resched_disable_count(), 1u, "");
    thread_resched_reenable();
    EXPECT_EQ(thread_resched_disable_count(), 0u, "");

    // Test nesting: multiple increments and decrements.
    thread_preempt_disable();
    thread_preempt_disable();
    EXPECT_EQ(thread_preempt_disable_count(), 2u, "");
    thread_preempt_reenable();
    thread_preempt_reenable();
    EXPECT_EQ(thread_preempt_disable_count(), 0u, "");

    // Test nesting: multiple increments and decrements.
    thread_resched_disable();
    thread_resched_disable();
    EXPECT_EQ(thread_resched_disable_count(), 2u, "");
    thread_resched_reenable();
    thread_resched_reenable();
    EXPECT_EQ(thread_resched_disable_count(), 0u, "");

    END_TEST;
}

static bool test_decrement_clears_preempt_pending() {
    BEGIN_TEST;

    thread_t* thread = get_current_thread();

    // Test that thread_preempt_reenable() clears preempt_pending.
    thread_preempt_disable();
    thread_reschedule();
    // It should not be possible for an interrupt handler to block or
    // otherwise cause a reschedule before our thread_preempt_reenable().
    EXPECT_EQ(thread->preempt_pending, true, "");
    thread_preempt_reenable();
    EXPECT_EQ(thread->preempt_pending, false, "");

    // Test that thread_resched_reenable() clears preempt_pending.
    thread_resched_disable();
    arch_disable_ints();
    thread_reschedule();
    // Read preempt_pending with interrupts disabled because otherwise an
    // interrupt handler could set it to false.
    EXPECT_EQ(thread->preempt_pending, true, "");
    arch_enable_ints();
    thread_resched_reenable();
    EXPECT_EQ(thread->preempt_pending, false, "");

    END_TEST;
}

static bool test_blocking_clears_preempt_pending() {
    BEGIN_TEST;

    thread_t* thread = get_current_thread();

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

    // It is OK to block while rescheduling is disabled.  In this case,
    // blocking should clear preempt_pending.
    thread_resched_disable();
    thread_reschedule();
    thread_sleep_relative(ZX_MSEC(10));
    EXPECT_EQ(thread->preempt_pending, false, "");
    thread_resched_reenable();

    END_TEST;
}

// Test that preempt_pending is preserved across an interrupt handler when
// resched_disable is set and when the interrupt handler does not cause a
// preemption.  This tests the int_handler_start()/finish() routines rather
// than the full interrupt handler.
static bool test_interrupt_preserves_preempt_pending() {
    BEGIN_TEST;

    thread_t* thread = get_current_thread();

    thread_resched_disable();
    // Do this with interrupts disabled so that a real interrupt does not
    // clear preempt_pending.
    arch_disable_ints();
    thread_reschedule();

    // Simulate an interrupt handler invocation.
    int_handler_saved_state_t state;
    int_handler_start(&state);
    EXPECT_EQ(thread_preempt_disable_count(), 1u, "");
    bool do_preempt = int_handler_finish(&state);

    EXPECT_EQ(do_preempt, false, "");
    EXPECT_EQ(thread->preempt_pending, true, "");
    arch_enable_ints();
    thread_resched_reenable();
    EXPECT_EQ(thread->preempt_pending, false, "");

    END_TEST;
}

// Test that resched_disable does not prevent preemption by an interrupt
// handler.
static bool test_interrupt_clears_preempt_pending() {
    BEGIN_TEST;

    thread_t* thread = get_current_thread();

    thread_resched_disable();
    thread_reschedule();
    // Spin until we detect that we have been preempted.  preempt_pending
    // should eventually get set to false because the scheduler should
    // preempt this thread.
    while (thread->preempt_pending) {}
    thread_resched_reenable();

    END_TEST;
}

// Timer callback used for testing.
static void timer_set_preempt_pending(timer_t* timer, zx_time_t now,
                                      void* arg) {
    auto* timer_ran = reinterpret_cast<fbl::atomic<bool>*>(arg);

    thread_preempt_set_pending();
    timer_ran->store(true);
}

static bool test_interrupt_with_preempt_disable() {
    BEGIN_TEST;

    thread_t* thread = get_current_thread();

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

static bool test_interrupt_with_resched_disable() {
    BEGIN_TEST;

    thread_t* thread = get_current_thread();

    // Test that resched_disable does not defer preemptions by interrupt
    // handlers.
    //
    // This assumes that timer_set() will run the callback on the same CPU
    // that we invoked it from.
    thread_resched_disable();
    fbl::atomic<bool> timer_ran(false);
    timer_t timer;
    timer_init(&timer);
    timer_set(&timer, current_time() + ZX_USEC(100), TIMER_SLACK_CENTER, 0,
              timer_set_preempt_pending, reinterpret_cast<void*>(&timer_ran));
    // Spin until timer_ran is set by the interrupt handler.
    while (!timer_ran.load()) {}
    // preempt_pending should be reset to false either on returning from
    // our timer interrupt, or by some other preemption.
    EXPECT_EQ(thread->preempt_pending, false, "");
    thread_resched_reenable();

    END_TEST;
}

UNITTEST_START_TESTCASE(preempt_disable_tests)
UNITTEST("test_in_timer_callback", test_in_timer_callback)
UNITTEST("test_inc_dec_disable_counts", test_inc_dec_disable_counts)
UNITTEST("test_decrement_clears_preempt_pending",
         test_decrement_clears_preempt_pending)
UNITTEST("test_blocking_clears_preempt_pending",
         test_blocking_clears_preempt_pending)
UNITTEST("test_interrupt_preserves_preempt_pending",
         test_interrupt_preserves_preempt_pending)
UNITTEST("test_interrupt_clears_preempt_pending",
         test_interrupt_clears_preempt_pending)
UNITTEST("test_interrupt_with_preempt_disable",
         test_interrupt_with_preempt_disable)
UNITTEST("test_interrupt_with_resched_disable",
         test_interrupt_with_resched_disable)
UNITTEST_END_TESTCASE(preempt_disable_tests, "preempt_disable_tests",
                      "preempt_disable_tests");
