// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>
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

#include <ktl/enforce.h>

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

  // Test an explicit Enable
  {
    AutoPreemptDisabler ap_disabler;
    ASSERT_EQ(1u, preemption_state.PreemptDisableCount());

    {
      // Create a defered disabler, and test that Enabling it before its disabled does nothing.
      AutoPreemptDisabler ap_disabler2{AutoPreemptDisabler::Defer};
      ASSERT_EQ(1u, preemption_state.PreemptDisableCount());

      ap_disabler2.Enable();
      ASSERT_EQ(1u, preemption_state.PreemptDisableCount());
    }
    ASSERT_EQ(1u, preemption_state.PreemptDisableCount());

    // Should be able to toggle enable and disable
    ap_disabler.Enable();
    ASSERT_EQ(0u, preemption_state.PreemptDisableCount());
    ap_disabler.Disable();
    ASSERT_EQ(1u, preemption_state.PreemptDisableCount());
    // Ending on Enable should result in no change after the disabler goes out of scope.
    ap_disabler.Enable();
    ASSERT_EQ(0u, preemption_state.PreemptDisableCount());
  }

  ASSERT_EQ(0u, preemption_state.PreemptDisableCount());

  END_TEST;
}

static bool test_auto_timeslice_extension() {
  BEGIN_TEST;

  PreemptionState& preemption_state = Thread::Current::preemption_state();

  // Basic.
  {
    ASSERT_TRUE(preemption_state.PreemptIsEnabled());
    {
      AutoExpiringPreemptDisabler guard(ZX_TIME_INFINITE);
      ASSERT_FALSE(preemption_state.PreemptIsEnabled());
    }
    ASSERT_TRUE(preemption_state.PreemptIsEnabled());
  }

  // Nested.  Only the outermost guard matters.
  {
    ASSERT_TRUE(preemption_state.PreemptIsEnabled());
    {
      AutoExpiringPreemptDisabler guard1(ZX_TIME_INFINITE);
      ASSERT_FALSE(preemption_state.PreemptIsEnabled());
      {
        AutoExpiringPreemptDisabler guard2(0);
        // Even though guard2's duration is 0, preemption should still be disabled because of
        // guard1's extension.
        ASSERT_FALSE(preemption_state.PreemptIsEnabled());
      }
      ASSERT_FALSE(preemption_state.PreemptIsEnabled());
    }
    ASSERT_TRUE(preemption_state.PreemptIsEnabled());
  }

  END_TEST;
}

// Verify that in certain contexts where preemption cannot immediately occur, unblocking a thread
// pinned to the current CPU will mark the CPU for preemption.
//
// This test covers three cases:
//
// 1. preemption is disabled
// 2. eager resched is disabled
// 3. timeslice extension
// 4. a spinlock is held
// 5. blocking is disallowed via |arch_set_blocking_disallowed|.
//
// See fxbug.dev/100545 for motivation.
static bool test_local_preempt_pending() {
  BEGIN_TEST;

  // First, define the common code that will be used in all cases.
  //
  // |setup_and_run_with| is used to setup test conditions and run the |func| test case.
  //
  // |func| receives an Event that it should signal to unblock the |waiter| thread (see below).
  using Func = bool(Event&);
  auto setup_and_run_with = [](Func func) -> bool {
    BEGIN_TEST;

    // Make sure we restore this thread's affinity.
    auto cleanup = fit::defer([affinity = Thread::Current::Get()->GetCpuAffinity()]() {
      Thread::Current::Get()->SetCpuAffinity(affinity);
    });

    struct Args {
      Event event;
      ktl::atomic<bool> started{false};
    } args;

    auto waiter = [](void* void_args) -> int {
      auto* args = reinterpret_cast<Args*>(void_args);
      // Let the other thread know that we're up and running and then wait to be signaled.
      args->started.store(true);
      args->event.Wait();
      return 0;
    };

    const cpu_num_t target_cpu = BOOT_CPU_ID;

    // Migrate the current thread to the target CPU and bind a |waiter| thread to the same CPU.
    const cpu_mask_t mask = cpu_num_to_mask(target_cpu);
    Thread::Current::Get()->SetCpuAffinity(mask);
    Thread* t = Thread::Create("test_local_preempt_pending", waiter, &args, DEFAULT_PRIORITY);
    auto cleanup_waiter = fit::defer([t, &args]() {
      args.event.Signal();
      t->Join(nullptr, ZX_TIME_INFINITE);
    });
    t->SetCpuAffinity(mask);

    // Start the |waiter| and spin until we know that it has started running and then blocked.
    t->Resume();
    while (true) {
      Thread::Current::Yield();
      Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
      if (args.started.load() && t->scheduler_state().state() == THREAD_BLOCKED) {
        break;
      }
    }

    // At this point we know the |waiter| is blocked on the event.
    EXPECT_TRUE(func(args.event));

    END_TEST;
  };

  // Now test each case using the common code above.

  // 1. Preemption disabled should cause a preemption event to become pending.
  EXPECT_TRUE(setup_and_run_with([](Event& event) -> bool {
    BEGIN_TEST;
    AutoPreemptDisabler apd;
    // Unblock the |waiter|.  Because we've got preemption disabled, a preemption event for the
    // local CPU should become pending.
    event.Signal();
    cpu_mask_t pending = Thread::Current::preemption_state().preempts_pending();
    EXPECT_NE(0u, pending);
    EXPECT_TRUE(pending | cpu_num_to_mask(arch_curr_cpu_num()));
    END_TEST;
  }));

  // 2. Eager resched disabled should cause a preemption event to become pending.
  EXPECT_TRUE(setup_and_run_with([](Event& event) -> bool {
    BEGIN_TEST;
    AutoEagerReschedDisabler aerd;
    // Unblock the |waiter|.  Because we've got eager resched disabled (which implies preempt
    // disable), a preemption event for the local CPU should become pending.
    event.Signal();
    cpu_mask_t pending = Thread::Current::preemption_state().preempts_pending();
    EXPECT_NE(0u, pending);
    EXPECT_TRUE(pending | cpu_num_to_mask(arch_curr_cpu_num()));
    END_TEST;
  }));

  // 3. A timeslice extension should cause a preemption event to become pending.
  EXPECT_TRUE(setup_and_run_with([](Event& event) -> bool {
    BEGIN_TEST;
    AutoExpiringPreemptDisabler guard(ZX_TIME_INFINITE);
    // Unblock the |waiter|.  Because we've got eager resched disabled (which implies preempt
    // disable), a preemption event for the local CPU should become pending.
    event.Signal();
    cpu_mask_t pending = Thread::Current::preemption_state().preempts_pending();
    EXPECT_NE(0u, pending);
    EXPECT_TRUE(pending | cpu_num_to_mask(arch_curr_cpu_num()));
    END_TEST;
  }));

  // 4. Holding a spinlock should cause a preemption event to become pending.
  EXPECT_TRUE(setup_and_run_with([](Event& event) {
    BEGIN_TEST;
    DECLARE_SINGLETON_SPINLOCK_WITH_TYPE(local_lock, MonitoredSpinLock);
    Guard<MonitoredSpinLock, IrqSave> guard{local_lock::Get(), SOURCE_TAG};
    // Unblock the |waiter|.  Because we're holding a spinlock, a preemption event for the local CPU
    // should become pending.
    event.Signal();
    cpu_mask_t pending = Thread::Current::preemption_state().preempts_pending();
    EXPECT_NE(0u, pending);
    EXPECT_TRUE(pending | cpu_num_to_mask(arch_curr_cpu_num()));
    END_TEST;
  }));

  // 5. arch_blocking_disallowed() should cause a preemption event to become pending.
  EXPECT_TRUE(setup_and_run_with([](Event& event) {
    BEGIN_TEST;
    // The fault handler may use the blocking disallowed state as a recursion check so be sure to
    // keep interrupts disabled when we've got blocking set to disallowed.
    InterruptDisableGuard irqd;
    arch_set_blocking_disallowed(true);
    auto cleanup = fit::defer([]() { arch_set_blocking_disallowed(false); });
    // Unblock the |waiter|.  Because blocking is disallowed, a preemption event for the local CPU
    // should become pending.
    event.Signal();
    cpu_mask_t pending = Thread::Current::preemption_state().preempts_pending();
    EXPECT_NE(0u, pending);
    EXPECT_TRUE(pending | cpu_num_to_mask(arch_curr_cpu_num()));
    END_TEST;
  }));

  END_TEST;
}

static bool test_evaluate_timeslice_extension() {
  BEGIN_TEST;

  // Nothing preventing preemption.
  PreemptionState& preemption_state = Thread::Current::preemption_state();
  ASSERT_TRUE(preemption_state.PreemptIsEnabled());
  ASSERT_TRUE(preemption_state.EvaluateTimesliceExtension());
  ASSERT_TRUE(preemption_state.PreemptIsEnabled());

  // Disabled (by count).
  {
    AutoPreemptDisabler apd;
    EXPECT_FALSE(preemption_state.PreemptIsEnabled());
    EXPECT_FALSE(preemption_state.EvaluateTimesliceExtension());
    EXPECT_FALSE(preemption_state.PreemptIsEnabled());
  }
  ASSERT_TRUE(preemption_state.PreemptIsEnabled());

  // Disabled (by eager resched count).
  {
    AutoEagerReschedDisabler aerd;
    EXPECT_FALSE(preemption_state.PreemptIsEnabled());
    EXPECT_FALSE(preemption_state.EvaluateTimesliceExtension());
    EXPECT_FALSE(preemption_state.PreemptIsEnabled());
  }
  ASSERT_TRUE(preemption_state.PreemptIsEnabled());

  // Disabled (by infinite timeslice extension).
  {
    AutoExpiringPreemptDisabler guard(ZX_TIME_INFINITE);
    EXPECT_FALSE(preemption_state.PreemptIsEnabled());
    EXPECT_FALSE(preemption_state.EvaluateTimesliceExtension());
    EXPECT_FALSE(preemption_state.PreemptIsEnabled());
  }
  ASSERT_TRUE(preemption_state.PreemptIsEnabled());

  // In the tests below, the current thread will defer preemption for
  // kEpsilonDuration and sleep for kEpsilonDuration.  The only requirement for
  // correctness is that this value is greater than zero.  We use the value 1 to
  // minimize test runtime.
  static constexpr zx_duration_t kEpsilonDuration = 1;

  // See that the timeslice extension expires.
  {
    AutoExpiringPreemptDisabler guard(kEpsilonDuration);
    // Note, we cannot reliably assert that preemption is disabled at this point
    // because a preemption request may have already occurred and
    // kEpsilonDuration may have already elapsed.
    Thread::Current::Reschedule();
    Thread::Current::SleepRelative(kEpsilonDuration);
    EXPECT_TRUE(preemption_state.EvaluateTimesliceExtension());
    EXPECT_TRUE(preemption_state.PreemptIsEnabled());
  }
  ASSERT_TRUE(preemption_state.PreemptIsEnabled());

  // AutoPreemptDisabler inside an expired AutoExpiringPreemptDisabler.
  {
    AutoExpiringPreemptDisabler guard1(kEpsilonDuration);
    AutoPreemptDisabler guard2;
    EXPECT_FALSE(preemption_state.PreemptIsEnabled());
    Thread::Current::Reschedule();
    Thread::Current::SleepRelative(kEpsilonDuration);
    EXPECT_FALSE(preemption_state.EvaluateTimesliceExtension());
    // Still false because of the APD.
    EXPECT_FALSE(preemption_state.PreemptIsEnabled());
  }
  ASSERT_TRUE(preemption_state.PreemptIsEnabled());

  // AutoEagerReschedDisabler inside an expired AutoExpiringPreemptDisabler.
  {
    AutoExpiringPreemptDisabler guard1(kEpsilonDuration);
    AutoEagerReschedDisabler guard2;
    EXPECT_FALSE(preemption_state.PreemptIsEnabled());
    Thread::Current::Reschedule();
    Thread::Current::SleepRelative(kEpsilonDuration);
    EXPECT_FALSE(preemption_state.EvaluateTimesliceExtension());
    // Still false because of the AERD.
    EXPECT_FALSE(preemption_state.PreemptIsEnabled());
  }
  ASSERT_TRUE(preemption_state.PreemptIsEnabled());

  END_TEST;
}

// This test simulates a race condition where a preemption is requested (via IPI
// or timer) concurrent with reenabling preemption / eager rescheduling while an
// inactive timeslice extension is in place.
static bool test_flush_race() {
  BEGIN_TEST;

  PreemptionState& preemption_state = Thread::Current::preemption_state();
  ASSERT_TRUE(preemption_state.PreemptIsEnabled());

  // Test PreemptReenable.
  {
    AutoExpiringPreemptDisabler guard(ZX_TIME_INFINITE);
    const cpu_mask_t curr_mask = cpu_num_to_mask(arch_curr_cpu_num());
    {
      AutoPreemptDisabler apd;
      ASSERT_FALSE(preemption_state.PreemptIsEnabled());

      // We'll simulate a race by marking the current CPU as pending for
      // preemption without going through the normal PreemptSetPending path
      // because we want to test the behavior when the extension is
      // inactive. and PreemptSetPending will activate it.
      preemption_state.preempts_pending_add(curr_mask);
      // When leave this scope and reenable preemption, we'd better not flush.
    }
    ASSERT_FALSE(preemption_state.PreemptIsEnabled());
    // See that we did not flush.
    ASSERT_EQ(curr_mask, preemption_state.preempts_pending());
  }

  // Test PreemptReenableDelayFlush.
  {
    AutoExpiringPreemptDisabler guard(ZX_TIME_INFINITE);
    const cpu_mask_t curr_mask = cpu_num_to_mask(arch_curr_cpu_num());
    bool should_preempt;
    {
      InterruptDisableGuard irqd;
      arch_set_blocking_disallowed(true);
      auto cleanup = fit::defer([]() { arch_set_blocking_disallowed(false); });
      preemption_state.PreemptDisable();
      preemption_state.preempts_pending_add(curr_mask);
      should_preempt = preemption_state.PreemptReenableDelayFlush();
    }
    ASSERT_FALSE(preemption_state.PreemptIsEnabled());
    ASSERT_FALSE(should_preempt);
  }

  // Test EagerReschedReenable.
  {
    AutoExpiringPreemptDisabler guard(ZX_TIME_INFINITE);
    const cpu_mask_t curr_mask = cpu_num_to_mask(arch_curr_cpu_num());
    {
      AutoEagerReschedDisabler aerd;
      ASSERT_FALSE(preemption_state.PreemptIsEnabled());
      preemption_state.preempts_pending_add(CPU_MASK_ALL);
    }
    ASSERT_FALSE(preemption_state.PreemptIsEnabled());
    // See that we flushed the remote CPUs, but not the local.
    ASSERT_EQ(curr_mask, preemption_state.preempts_pending());
  }

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
UNITTEST("test_auto_timeslice_extension", test_auto_timeslice_extension)
UNITTEST("test_local_preempt_pending", test_local_preempt_pending)
UNITTEST("test_evaluate_timeslice_extension", test_evaluate_timeslice_extension)
UNITTEST("test_flush_race", test_flush_race)
UNITTEST_END_TESTCASE(preempt_disable_tests, "preempt_disable_tests", "preempt_disable_tests")
