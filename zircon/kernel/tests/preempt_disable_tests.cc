// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/zircon-internal/macros.h>
#include <platform.h>

#include <arch/ops.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/cpu.h>
#include <kernel/event.h>
#include <kernel/interrupt.h>
#include <kernel/scheduler.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <kernel/timer.h>
#include <ktl/atomic.h>

class PreemptDisableTestAccess {
 public:
  struct State {
    cpu_mask_t preempts_pending;
  };
  static State SaveState(PreemptionState* preemption_state) {
    return {preemption_state->preempts_pending()};
  }
  static void RestoreState(PreemptionState* preemption_state, State state) {
    preemption_state->preempts_pending_ = state.preempts_pending;
  }
  static void ClearPending(PreemptionState* preemption_state) {
    preemption_state->preempts_pending_ = 0;
  }
};

// Test that PreemptDisable is set for timer callbacks and that, in this
// context, preempts_pending will get set by some functions.
static void timer_callback_func(Timer* timer, zx_time_t now, void* arg) {
  Event* event = (Event*)arg;

  // The timer should run in interrupt context.
  ASSERT(arch_ints_disabled());
  ASSERT(arch_blocking_disallowed());

  // Entry into interrupt context should disable preemption and eager
  // reschedules.
  PreemptionState& preemption_state = Thread::Current::preemption_state();
  ASSERT(preemption_state.PreemptDisableCount() > 0);
  ASSERT(preemption_state.EagerReschedDisableCount() > 0);
  PreemptDisableTestAccess::State state = PreemptDisableTestAccess::SaveState(&preemption_state);

  // Test that Scheduler::Reschedule() sets the preempt_pending flag when
  // PreemptDisable is set.
  PreemptDisableTestAccess::ClearPending(&preemption_state);
  ASSERT(preemption_state.preempts_pending() == 0);
  {
    Guard<MonitoredSpinLock, NoIrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
    Scheduler::Reschedule();
  }
  ASSERT(preemption_state.preempts_pending() != 0);

  // Test that preemption_state.PreemptSetPending() sets preempts_pending.
  PreemptDisableTestAccess::ClearPending(&preemption_state);
  ASSERT(preemption_state.preempts_pending() == 0);

  preemption_state.PreemptSetPending();
  ASSERT(preemption_state.preempts_pending() != 0);

  PreemptDisableTestAccess::RestoreState(&preemption_state, state);
  event->Signal();
}

// Schedule a timer callback and wait for it to complete.  Most of the
// testing is done in the timer callback.
static bool test_in_timer_callback() {
  BEGIN_TEST;

  Event event;
  Timer timer;

  timer.Set(Deadline::no_slack(0), timer_callback_func, &event);
  ASSERT_EQ(event.Wait(), ZX_OK);

  // Make sure the timer has fully completed prior to letting it go out of scope.
  timer.Cancel();

  END_TEST;
}

// Test incrementing and decrementing the PreemptDisable and
// EagerReschedDisable counts.
static bool test_inc_dec_disable_counts() {
  BEGIN_TEST;

  PreemptionState& preemption_state = Thread::Current::preemption_state();

  // Test initial conditions.
  ASSERT_EQ(preemption_state.PreemptDisableCount(), 0u);
  ASSERT_EQ(preemption_state.EagerReschedDisableCount(), 0u);
  // While preemption is allowed, a preemption should not be pending.
  ASSERT_EQ(preemption_state.preempts_pending(), 0u);

  // Test incrementing and decrementing of PreemptDisable.
  preemption_state.PreemptDisable();
  EXPECT_EQ(preemption_state.PreemptDisableCount(), 1u);
  preemption_state.PreemptReenable();
  EXPECT_EQ(preemption_state.PreemptDisableCount(), 0u);

  // Test incrementing and decrementing of EagerReschedDisable.
  preemption_state.EagerReschedDisable();
  EXPECT_EQ(preemption_state.EagerReschedDisableCount(), 1u);
  preemption_state.EagerReschedReenable();
  EXPECT_EQ(preemption_state.EagerReschedDisableCount(), 0u);

  // Test nesting: multiple increments and decrements.
  preemption_state.PreemptDisable();
  preemption_state.PreemptDisable();
  EXPECT_EQ(preemption_state.PreemptDisableCount(), 2u);
  preemption_state.PreemptReenable();
  preemption_state.PreemptReenable();
  EXPECT_EQ(preemption_state.PreemptDisableCount(), 0u);

  // Test nesting: multiple increments and decrements.
  preemption_state.EagerReschedDisable();
  preemption_state.EagerReschedDisable();
  EXPECT_EQ(preemption_state.EagerReschedDisableCount(), 2u);
  preemption_state.EagerReschedReenable();
  preemption_state.EagerReschedReenable();
  EXPECT_EQ(preemption_state.EagerReschedDisableCount(), 0u);

  END_TEST;
}

static bool test_decrement_clears_preempt_pending() {
  BEGIN_TEST;

  PreemptionState& preemption_state = Thread::Current::preemption_state();
  ASSERT_TRUE(preemption_state.PreemptIsEnabled());
  ASSERT_EQ(preemption_state.preempts_pending(), 0u);

  // Test that preemption_state.PreemptReenable() clears preempt_pending.
  preemption_state.PreemptDisable();
  Thread::Current::Reschedule();
  EXPECT_NE(preemption_state.preempts_pending(), 0u);
  preemption_state.PreemptReenable();
  EXPECT_EQ(preemption_state.preempts_pending(), 0u);

  // Test that preemption_state.EagerReschedReenable() clears preempt_pending.
  preemption_state.EagerReschedDisable();
  Thread::Current::Reschedule();
  EXPECT_NE(preemption_state.preempts_pending(), 0u);
  preemption_state.EagerReschedReenable();
  EXPECT_EQ(preemption_state.preempts_pending(), 0u);

  END_TEST;
}

static bool test_blocking_clears_preempt_pending() {
  BEGIN_TEST;

  PreemptionState& preemption_state = Thread::Current::preemption_state();

  // It is OK to block while preemption is disabled. In this case, blocking
  // should clear a pending local preemption.
  preemption_state.PreemptDisable();
  Thread::Current::Reschedule();
  EXPECT_NE(preemption_state.preempts_pending(), 0u);
  interrupt_saved_state_t int_state = arch_interrupt_save();
  Thread::Current::SleepRelative(ZX_MSEC(10));
  // Read preempts_pending with interrupts disabled because otherwise an
  // interrupt handler could set it.
  EXPECT_EQ(preemption_state.preempts_pending(), 0u);
  arch_interrupt_restore(int_state);
  preemption_state.PreemptReenable();

  // It is OK to block while eager rescheduling is disabled. In this case,
  // blocking should clear all pending preemptions.
  preemption_state.EagerReschedDisable();
  Thread::Current::Reschedule();
  int_state = arch_interrupt_save();
  Thread::Current::SleepRelative(ZX_MSEC(10));
  // Read preempts_pending with interrupts disabled because otherwise an
  // interrupt handler could set it.
  EXPECT_EQ(preemption_state.preempts_pending(), 0u);
  arch_interrupt_restore(int_state);
  preemption_state.EagerReschedReenable();

  END_TEST;
}

// Test that preempts_pending is preserved across an interrupt handler when
// EagerReschedDisable is set and when the interrupt handler does not cause a
// preemption. This tests the int_handler_start()/finish() routines rather than
// the full interrupt handler.
static bool test_interrupt_preserves_preempt_pending() {
  BEGIN_TEST;

  PreemptionState& preemption_state = Thread::Current::preemption_state();

  preemption_state.EagerReschedDisable();
  // Do this with interrupts disabled so that a real interrupt does not
  // clear preempts_pending.
  interrupt_saved_state_t int_state = arch_interrupt_save();
  Thread::Current::Reschedule();

  // Simulate an interrupt handler invocation.
  int_handler_saved_state_t state;
  int_handler_start(&state);
  EXPECT_EQ(preemption_state.PreemptDisableCount(), 1u);
  bool do_preempt = int_handler_finish(&state);

  EXPECT_EQ(do_preempt, false);
  EXPECT_NE(preemption_state.preempts_pending(), 0u);
  arch_interrupt_restore(int_state);
  preemption_state.EagerReschedReenable();
  EXPECT_EQ(preemption_state.preempts_pending(), 0u);

  END_TEST;
}

// Timer callback used for testing.
static void timer_set_preempt_pending(Timer* timer, zx_time_t now, void* arg) {
  auto* timer_ran = reinterpret_cast<ktl::atomic<bool>*>(arg);

  Thread::Current::preemption_state().PreemptSetPending(cpu_num_to_mask(arch_curr_cpu_num()));
  timer_ran->store(true);
}

static bool test_interrupt_with_preempt_disable() {
  BEGIN_TEST;

  PreemptionState& preemption_state = Thread::Current::preemption_state();

  // Test that interrupt handlers honor PreemptDisable.
  //
  // We test that by setting a timer callback that will set
  // preempt_pending from inside an interrupt handler.  preempt_pending
  // should remain set after the interrupt handler returns.
  //
  // This assumes that timer_set() will run the callback on the same CPU
  // that we invoked it from.  This also assumes that we don't
  // accidentally call any blocking operations that cause our thread to
  // be rescheduled to another CPU.
  preemption_state.PreemptDisable();
  ktl::atomic<bool> timer_ran(false);
  Timer timer;
  const Deadline deadline = Deadline::after(ZX_USEC(100));
  timer.Set(deadline, timer_set_preempt_pending, reinterpret_cast<void*>(&timer_ran));
  // Spin until timer_ran is set by the interrupt handler.
  while (!timer_ran.load()) {
  }
  EXPECT_EQ(preemption_state.preempts_pending(), cpu_num_to_mask(arch_curr_cpu_num()));
  preemption_state.PreemptReenable();

  // Make sure the timer has fully completed prior to letting it go out of scope.
  timer.Cancel();

  END_TEST;
}

static bool test_auto_preempt_disabler() {
  BEGIN_TEST;

  PreemptionState& preemption_state = Thread::Current::preemption_state();

  // Make sure that nothing funny is going on with our preempt disable count as
  // it stands now.
  ASSERT_EQ(0u, preemption_state.PreemptDisableCount());

  {
    // Create a disabler inside of a scope, but do not have it immediately
    // request that preemption be disabled.  Our count should still be zero.
    AutoPreemptDisabler ap_disabler{AutoPreemptDisabler::Defer};
    ASSERT_EQ(0u, preemption_state.PreemptDisableCount());

    // Now explicitly disable.  Our count should go to 1.
    ap_disabler.Disable();
    ASSERT_EQ(1u, preemption_state.PreemptDisableCount());

    // Do it again, our count should remain at 1.
    ap_disabler.Disable();
    ASSERT_EQ(1u, preemption_state.PreemptDisableCount());

    {
      // Make another inside of a new scope.  Our count should remain at 1 until
      // we explicitly use the new instance to disable preemption.
      AutoPreemptDisabler ap_disabler2{AutoPreemptDisabler::Defer};
      ASSERT_EQ(1u, preemption_state.PreemptDisableCount());

      ap_disabler2.Disable();
      ASSERT_EQ(2u, preemption_state.PreemptDisableCount());
    }  // Let it go out of scope, we should drop down to a count of 1.

    ASSERT_EQ(1u, preemption_state.PreemptDisableCount());
  }  // Allow the original to go out of scope.  This should get us back down to a count of 0.

  ASSERT_EQ(0u, preemption_state.PreemptDisableCount());

  // Next, do a similar test, but this time with the version which automatically
  // begins life with preemption disabled.
  {
    AutoPreemptDisabler ap_disabler;
    ASSERT_EQ(1u, preemption_state.PreemptDisableCount());

    // Attempting to call disable should do nothing.
    ap_disabler.Disable();
    ASSERT_EQ(1u, preemption_state.PreemptDisableCount());

    {
      // Add a second.  Watch the count go up as it comes into scope, and back
      // down again when it goes out.
      AutoPreemptDisabler ap_disabler2;
      ASSERT_EQ(2u, preemption_state.PreemptDisableCount());
    }

    ASSERT_EQ(1u, preemption_state.PreemptDisableCount());
  }  // Allow the original to go out of scope.  This should get us back down to a count of 0.

  ASSERT_EQ(0u, preemption_state.PreemptDisableCount());

  END_TEST;
}

UNITTEST_START_TESTCASE(preempt_disable_tests)
UNITTEST("test_in_timer_callback", test_in_timer_callback)
UNITTEST("test_inc_dec_disable_counts", test_inc_dec_disable_counts)
UNITTEST("test_decrement_clears_preempt_pending", test_decrement_clears_preempt_pending)
UNITTEST("test_blocking_clears_preempt_pending", test_blocking_clears_preempt_pending)
UNITTEST("test_interrupt_preserves_preempt_pending", test_interrupt_preserves_preempt_pending)
UNITTEST("test_interrupt_with_preempt_disable", test_interrupt_with_preempt_disable)
UNITTEST("test_auto_preempt_disabler", test_auto_preempt_disabler)
UNITTEST_END_TESTCASE(preempt_disable_tests, "preempt_disable_tests", "preempt_disable_tests")
