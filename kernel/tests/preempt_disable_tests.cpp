// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/event.h>
#include <kernel/sched.h>
#include <kernel/timer.h>
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
static bool test_in_timer_callback(void* context) {
    BEGIN_TEST;

    event_t event;
    event_init(&event, false, 0);

    timer_t timer;
    timer_init(&timer);
    timer_set(&timer, 0, TIMER_SLACK_CENTER, 0, timer_callback_func, &event);

    REQUIRE_EQ(event_wait(&event), ZX_OK, "");
    event_destroy(&event);

    END_TEST;
}

UNITTEST_START_TESTCASE(preempt_disable_tests)
UNITTEST("test_in_timer_callback", test_in_timer_callback)
UNITTEST_END_TESTCASE(preempt_disable_tests, "preempt_disable_tests",
                      "preempt_disable_tests", nullptr, nullptr);
