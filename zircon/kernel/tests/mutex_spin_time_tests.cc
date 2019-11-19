// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <platform.h>

#include <fbl/auto_call.h>
#include <kernel/auto_preempt_disabler.h>
#include <kernel/mp.h>
#include <ktl/atomic.h>
#include <ktl/popcount.h>
#include <lib/affine/ratio.h>
#include <lib/zx/time.h>

#include "tests.h"

namespace {
bool mutex_spin_time_test(void) {
  BEGIN_TEST;

  // We cannot run this test unless there are at least 2 CPUs currently online.  Either find two
  // cores we can use, or just skip the test with a warning message.
  cpu_mask_t timer_mask;
  cpu_mask_t spinner_mask;
  {
    cpu_mask_t avail_mask = mp_get_online_mask();
    int avail_count = ktl::popcount(avail_mask);
    if (avail_count < 2) {
      printf("Insufficient cores online to run the mutex spin timeout tests.  Skipping!\n");
      END_TEST;
    }

    for (timer_mask = 0x1; (timer_mask & avail_mask) == 0; timer_mask <<= 1)
      ;

    for (spinner_mask = timer_mask << 1; (spinner_mask & avail_mask) == 0; spinner_mask <<= 1)
      ;
  }

  // No matter what happens from here on out, make sure we restore our main
  // thread's priority and cpu affinity.
  auto cleanup = fbl::MakeAutoCall([affinity = thread_get_cpu_affinity(get_current_thread()),
                                    priority = get_current_thread()->base_priority]() {
    thread_set_cpu_affinity(get_current_thread(), affinity);
    thread_set_priority(get_current_thread(), priority);
  });

  constexpr zx::duration kTimeouts[] = {
    zx::usec(0),
    zx::usec(50),
    zx::usec(250),
    zx::usec(750),
    zx::usec(5000),
  };

  const affine::Ratio ticks_to_time = platform_get_ticks_to_time_ratio();

  struct Args {
    DECLARE_MUTEX(Args) the_mutex;
    zx::duration spin_max_duration;
    ktl::atomic<bool> interlock{false};
  } args;

  // Our test thunk is very simple.  One we are started, we disable preemption
  // and then signal the timer thread via the interlock atomic.  Once the timer
  // thread has ack'ed our signal, we just grab and release the test mutex with
  // the specified spin timeout.
  auto thunk = [](void* ctx) -> int {
    auto& args = *(static_cast<Args*>(ctx));

    AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> ap_disabler;
    args.interlock.store(true);
    while (args.interlock.load() == true) {
        arch_spinloop_pause();
    }

    Guard<Mutex> guard{&args.the_mutex, args.spin_max_duration.get()};
    return 0;
  };

  // Boost our thread priority and lock ourselves down to a specific CPU before
  // starting the test.
  thread_set_cpu_affinity(get_current_thread(), timer_mask);
  thread_set_priority(get_current_thread(), HIGH_PRIORITY);

  for (const auto& timeout : kTimeouts) {
    zx::ticks start, end;
    thread_t* test_thread;

    // Setup the timeout and create the test thread (but don't start it yet).
    // Make sure that the thread runs on a different core from ours.
    args.spin_max_duration = timeout;
    test_thread = thread_create("mutex spin timeout", thunk, &args, HIGH_PRIORITY);
    ASSERT_NONNULL(test_thread, "Failed to create test thread");
    thread_set_cpu_affinity(test_thread, spinner_mask);

    // Hold onto the mutex while we create a thread and time how long it takes for the mutex to
    // become blocked.
    {
      AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> ap_disabler;
      Guard<Mutex> guard{&args.the_mutex};
      thread_resume(test_thread);

      // Wait until the spinner thread is ready to go, then mark the start time
      // and tell the spinner it is OK to proceed.
      while (args.interlock.load() == false) {
          arch_spinloop_pause();
      }
      start = zx::ticks(current_ticks());
      args.interlock.store(false);

      // Spin until we notice that the thread is blocked.
      thread_state s;
      while (true) {
        {
          Guard<spin_lock_t, IrqSave> thread_lock_guard{ThreadLock::Get()};
          s = test_thread->state;
        }

        if (s == THREAD_BLOCKED) {
          break;
        }

        arch_spinloop_pause();
      }

      end = zx::ticks(current_ticks());
    }

    // Now that we are out of the lock, clean up the test thread and check our
    // timing.  We should have spun for at _least_ the time specified.  For the
    // benefit of a human test runner/observer, also print out how much over the
    // limit we ended up.  There is technically no upper bound to this number,
    // but we would like to observe the overshoot amount as being "reasonable"
    // in an unloaded manual test environment.
    zx_status_t status = thread_join(test_thread, nullptr, current_time() + ZX_SEC(30));
    ASSERT_EQ(status, ZX_OK, "test thread failed to exit!");

    zx::duration actual_spin_time(ticks_to_time.Scale((end - start).get()));
    EXPECT_GE(actual_spin_time.get(), timeout.get(), "Didn't spin for long enough!");

    if (timeout.get() > 0) {
      int64_t overshoot = (((actual_spin_time - timeout).get()) * 10000) / timeout.get();
      printf("Target %7ld nSec, Actual %7ld nSec.  Overshot by %ld.%02ld%%.\n",
          timeout.get(), actual_spin_time.get(), overshoot / 100, overshoot % 100);
    } else {
      printf("\nTarget %7ld nSec, Actual %7ld nSec.\n", timeout.get(), actual_spin_time.get());
    }
  }

  END_TEST;
}
}

UNITTEST_START_TESTCASE(mutex_spin_time_tests)
UNITTEST("Mutex spin timeouts", (mutex_spin_time_test))
UNITTEST_END_TESTCASE(mutex_spin_time_tests, "mutex_spin_time", "mutex_spin_time tests")
