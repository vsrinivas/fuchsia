// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <platform.h>

#include <kernel/auto_preempt_disabler.h>
#include <kernel/event.h>
#include <kernel/interrupt.h>
#include <kernel/sched.h>
#include <kernel/thread_lock.h>
#include <kernel/timer.h>
#include <ktl/atomic.h>

// Test that preempt_disable is set for timer callbacks and that, in this
// context, preempt_pending will get set by some functions.
static void timer_callback_func(timer_t* timer, zx_time_t now, void* arg) {
  event_t* event = (event_t*)arg;

  // Timer callbacks should be called in interrupt context with
  // preempt_disable set.
  ASSERT(arch_ints_disabled());
  ASSERT(arch_blocking_disallowed());
  Thread* thread = Thread::Current::Get();
  ASSERT(Thread::Current::PreemptDisableCount() > 0);

  // Save and restore the value of preempt_pending so that we can test
  // other functions' behavior with preempt_pending==false.  It is
  // possible that preempt_pending is true now: it might have been set by
  // another timer callback.
  bool old_preempt_pending = thread->preempt_pending_;

  // Test that sched_reschedule() sets the preempt_pending flag when
  // preempt_disable is set.
  thread->preempt_pending_ = false;
  {
    Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
    sched_reschedule();
  }
  ASSERT(thread->preempt_pending_);

  // Test that Thread::Current::PreemptSetPending() sets the preempt_pending
  // flag.
  thread->preempt_pending_ = false;
  Thread::Current::PreemptSetPending();
  ASSERT(thread->preempt_pending_);

  // Restore value.
  thread->preempt_pending_ = old_preempt_pending;

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
  timer_set(&timer, Deadline::no_slack(0), timer_callback_func, &event);

  ASSERT_EQ(event_wait(&event), ZX_OK);
  event_destroy(&event);

  END_TEST;
}

// Test incrementing and decrementing the preempt_disable and
// resched_disable counts.
static bool test_inc_dec_disable_counts() {
  BEGIN_TEST;

  Thread* thread = Thread::Current::Get();

  // Test initial conditions.
  ASSERT_EQ(Thread::Current::PreemptDisableCount(), 0u);
  ASSERT_EQ(Thread::Current::ReschedDisableCount(), 0u);
  // While preemption is allowed, a preemption should not be pending.
  ASSERT_EQ(thread->preempt_pending_, false);

  // Test incrementing and decrementing of preempt_disable.
  Thread::Current::PreemptDisable();
  EXPECT_EQ(Thread::Current::PreemptDisableCount(), 1u);
  Thread::Current::PreemptReenable();
  EXPECT_EQ(Thread::Current::PreemptDisableCount(), 0u);

  // Test incrementing and decrementing of resched_disable.
  Thread::Current::ReschedDisable();
  EXPECT_EQ(Thread::Current::ReschedDisableCount(), 1u);
  Thread::Current::ReschedReenable();
  EXPECT_EQ(Thread::Current::ReschedDisableCount(), 0u);

  // Test nesting: multiple increments and decrements.
  Thread::Current::PreemptDisable();
  Thread::Current::PreemptDisable();
  EXPECT_EQ(Thread::Current::PreemptDisableCount(), 2u);
  Thread::Current::PreemptReenable();
  Thread::Current::PreemptReenable();
  EXPECT_EQ(Thread::Current::PreemptDisableCount(), 0u);

  // Test nesting: multiple increments and decrements.
  Thread::Current::ReschedDisable();
  Thread::Current::ReschedDisable();
  EXPECT_EQ(Thread::Current::ReschedDisableCount(), 2u);
  Thread::Current::ReschedReenable();
  Thread::Current::ReschedReenable();
  EXPECT_EQ(Thread::Current::ReschedDisableCount(), 0u);

  END_TEST;
}

static bool test_decrement_clears_preempt_pending() {
  BEGIN_TEST;

  Thread* thread = Thread::Current::Get();

  // Test that Thread::Current::PreemptReenable() clears preempt_pending.
  Thread::Current::PreemptDisable();
  Thread::Current::Reschedule();
  // It should not be possible for an interrupt handler to block or
  // otherwise cause a reschedule before our Thread::Current::PreemptReenable().
  EXPECT_EQ(thread->preempt_pending_, true);
  Thread::Current::PreemptReenable();
  EXPECT_EQ(thread->preempt_pending_, false);

  // Test that Thread::Current::ReschedReenable() clears preempt_pending.
  Thread::Current::ReschedDisable();
  arch_disable_ints();
  Thread::Current::Reschedule();
  // Read preempt_pending with interrupts disabled because otherwise an
  // interrupt handler could set it to false.
  EXPECT_EQ(thread->preempt_pending_, true);
  arch_enable_ints();
  Thread::Current::ReschedReenable();
  EXPECT_EQ(thread->preempt_pending_, false);

  END_TEST;
}

static bool test_blocking_clears_preempt_pending() {
  BEGIN_TEST;

  Thread* thread = Thread::Current::Get();

  // It is OK to block while preemption is disabled.  In this case,
  // blocking should clear preempt_pending.
  Thread::Current::PreemptDisable();
  Thread::Current::Reschedule();
  EXPECT_EQ(thread->preempt_pending_, true);
  arch_disable_ints();
  Thread::Current::SleepRelative(ZX_MSEC(10));
  // Read preempt_pending with interrupts disabled because otherwise an
  // interrupt handler could set it to true.
  EXPECT_EQ(thread->preempt_pending_, false);
  arch_enable_ints();
  Thread::Current::PreemptReenable();

  // It is OK to block while rescheduling is disabled.  In this case,
  // blocking should clear preempt_pending.
  Thread::Current::ReschedDisable();
  Thread::Current::Reschedule();
  Thread::Current::SleepRelative(ZX_MSEC(10));
  EXPECT_EQ(thread->preempt_pending_, false);
  Thread::Current::ReschedReenable();

  END_TEST;
}

// Test that preempt_pending is preserved across an interrupt handler when
// resched_disable is set and when the interrupt handler does not cause a
// preemption.  This tests the int_handler_start()/finish() routines rather
// than the full interrupt handler.
static bool test_interrupt_preserves_preempt_pending() {
  BEGIN_TEST;

  Thread* thread = Thread::Current::Get();

  Thread::Current::ReschedDisable();
  // Do this with interrupts disabled so that a real interrupt does not
  // clear preempt_pending.
  arch_disable_ints();
  Thread::Current::Reschedule();

  // Simulate an interrupt handler invocation.
  int_handler_saved_state_t state;
  int_handler_start(&state);
  EXPECT_EQ(Thread::Current::PreemptDisableCount(), 1u);
  bool do_preempt = int_handler_finish(&state);

  EXPECT_EQ(do_preempt, false);
  EXPECT_EQ(thread->preempt_pending_, true);
  arch_enable_ints();
  Thread::Current::ReschedReenable();
  EXPECT_EQ(thread->preempt_pending_, false);

  END_TEST;
}

// Test that resched_disable does not prevent preemption by an interrupt
// handler.
static bool test_interrupt_clears_preempt_pending() {
  BEGIN_TEST;

  Thread* thread = Thread::Current::Get();

  Thread::Current::ReschedDisable();
  Thread::Current::Reschedule();
  // Spin until we detect that we have been preempted.  preempt_pending
  // should eventually get set to false because the scheduler should
  // preempt this thread.
  while (thread->preempt_pending_) {
  }
  Thread::Current::ReschedReenable();

  END_TEST;
}

// Timer callback used for testing.
static void timer_set_preempt_pending(timer_t* timer, zx_time_t now, void* arg) {
  auto* timer_ran = reinterpret_cast<ktl::atomic<bool>*>(arg);

  Thread::Current::PreemptSetPending();
  timer_ran->store(true);
}

static bool test_interrupt_with_preempt_disable() {
  BEGIN_TEST;

  Thread* thread = Thread::Current::Get();

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
  Thread::Current::PreemptDisable();
  ktl::atomic<bool> timer_ran(false);
  timer_t timer;
  timer_init(&timer);
  const Deadline deadline = Deadline::no_slack(current_time() + ZX_USEC(100));
  timer_set(&timer, deadline, timer_set_preempt_pending, reinterpret_cast<void*>(&timer_ran));
  // Spin until timer_ran is set by the interrupt handler.
  while (!timer_ran.load()) {
  }
  EXPECT_EQ(thread->preempt_pending_, true);
  Thread::Current::PreemptReenable();

  END_TEST;
}

static bool test_interrupt_with_resched_disable() {
  BEGIN_TEST;

  Thread* thread = Thread::Current::Get();

  // Test that resched_disable does not defer preemptions by interrupt
  // handlers.
  //
  // This assumes that timer_set() will run the callback on the same CPU
  // that we invoked it from.
  Thread::Current::ReschedDisable();
  ktl::atomic<bool> timer_ran(false);
  timer_t timer;
  timer_init(&timer);
  const Deadline deadline = Deadline::no_slack(current_time() + ZX_USEC(100));
  timer_set(&timer, deadline, timer_set_preempt_pending, reinterpret_cast<void*>(&timer_ran));
  // Spin until timer_ran is set by the interrupt handler.
  while (!timer_ran.load()) {
  }
  // preempt_pending should be reset to false either on returning from
  // our timer interrupt, or by some other preemption.
  EXPECT_EQ(thread->preempt_pending_, false);
  Thread::Current::ReschedReenable();

  END_TEST;
}

static bool test_auto_preempt_disabler() {
  BEGIN_TEST;

  // Make sure that nothing funny is going on with our preempt disable count as
  // it stands now.
  ASSERT_EQ(0u, Thread::Current::PreemptDisableCount());

  {
    // Create a disabler inside of a scope, but do not have it immediately
    // request that preemption be disabled.  Our count should still be zero.
    AutoPreemptDisabler<APDInitialState::PREEMPT_ALLOWED> ap_disabler;
    ASSERT_EQ(0u, Thread::Current::PreemptDisableCount());

    // Now explicitly disable.  Our count should go to 1.
    ap_disabler.Disable();
    ASSERT_EQ(1u, Thread::Current::PreemptDisableCount());

    // Do it again, our count should remain at 1.
    ap_disabler.Disable();
    ASSERT_EQ(1u, Thread::Current::PreemptDisableCount());

    {
      // Make another inside of a new scope.  Our count should remain at 1 until
      // we explicitly use the new instance to disable preemption.
      AutoPreemptDisabler<APDInitialState::PREEMPT_ALLOWED> ap_disabler2;
      ASSERT_EQ(1u, Thread::Current::PreemptDisableCount());

      ap_disabler2.Disable();
      ASSERT_EQ(2u, Thread::Current::PreemptDisableCount());
    }  // Let it go out of scope, we should drop down to a count of 1.

    ASSERT_EQ(1u, Thread::Current::PreemptDisableCount());
  }  // Allow the original to go out of scope.  This should get us back down to a count of 0.

  ASSERT_EQ(0u, Thread::Current::PreemptDisableCount());

  // Next, do a similar test, but this time with the version which automatically
  // begins life with preemption disabled.  These versions are a bit simpler
  // under the hood as they do not require any internal state tracking.
  {
    AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> ap_disabler;
    ASSERT_EQ(1u, Thread::Current::PreemptDisableCount());

#if TEST_WILL_NOT_COMPILE || 0
    // Attempting to call disable should fail to build.
    ap_disabler.Disable();
#endif

    {
      // Add a second.  Watch the count go up as it comes into scope, and back
      // down again when it goes out.
      AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> ap_disabler2;
      ASSERT_EQ(2u, Thread::Current::PreemptDisableCount());
    }

    ASSERT_EQ(1u, Thread::Current::PreemptDisableCount());
  }  // Allow the original to go out of scope.  This should get us back down to a count of 0.

  ASSERT_EQ(0u, Thread::Current::PreemptDisableCount());

  END_TEST;
}

UNITTEST_START_TESTCASE(preempt_disable_tests)
UNITTEST("test_in_timer_callback", test_in_timer_callback)
UNITTEST("test_inc_dec_disable_counts", test_inc_dec_disable_counts)
UNITTEST("test_decrement_clears_preempt_pending", test_decrement_clears_preempt_pending)
UNITTEST("test_blocking_clears_preempt_pending", test_blocking_clears_preempt_pending)
UNITTEST("test_interrupt_preserves_preempt_pending", test_interrupt_preserves_preempt_pending)
UNITTEST("test_interrupt_clears_preempt_pending", test_interrupt_clears_preempt_pending)
UNITTEST("test_interrupt_with_preempt_disable", test_interrupt_with_preempt_disable)
UNITTEST("test_interrupt_with_resched_disable", test_interrupt_with_resched_disable)
UNITTEST("test_auto_preempt_disabler", test_auto_preempt_disabler)
UNITTEST_END_TESTCASE(preempt_disable_tests, "preempt_disable_tests", "preempt_disable_tests")
