// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>
#include <lib/unittest/unittest.h>
#include <lib/zircon-internal/macros.h>

#include <kernel/event.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>

// This tests that the result in an event_signal_etc call is propagated to the waiter
// when the event is signaled before any thread waits on the event.
static bool event_signal_result_before_wait_test() {
  BEGIN_TEST;

  Event event;

  zx_status_t signal_result = zx_status_t{42};
  event.Signal(signal_result);

  zx_status_t wake_result = event.Wait();

  EXPECT_EQ(wake_result, signal_result, "");

  END_TEST;
}

struct event_waiter_args {
  Event* event;
  zx_status_t wake_result;
};

static int event_waiter_thread(void* arg) {
  event_waiter_args* event_args = reinterpret_cast<event_waiter_args*>(arg);
  event_args->wake_result = event_args->event->Wait();
  return 0;
}

// This tests that the result in an SignalEtc call is propagated to the waiter
// when the waiter enters a blocking state before the event is signaled.
static bool event_signal_result_after_wait_test() {
  BEGIN_TEST;

  Event event;

  zx_status_t signal_result = zx_status_t{42};

  event_waiter_args args = {&event, ZX_OK};

  Thread* waiter =
      Thread::Create("event waiter thread", &event_waiter_thread, &args, DEFAULT_PRIORITY);
  waiter->Resume();

  int64_t wait_duration = ZX_USEC(1);
  while (true) {
    {
      // Check if the waiter thread is in the blocked state, indicating that the event
      // has latched.
      Guard<MonitoredSpinLock, IrqSave> guard{ThreadLock::Get(), SOURCE_TAG};
      if (waiter->state() == THREAD_BLOCKED) {
        break;
      }
    }
    // Nope - sleep and try again.
    Thread::Current::SleepRelative(wait_duration);
    wait_duration *= 2;
  }

  event.Signal(signal_result);

  int thread_retcode = 0;
  waiter->Join(&thread_retcode, ZX_TIME_INFINITE);

  ASSERT_EQ(thread_retcode, 0, "");

  EXPECT_EQ(args.wake_result, signal_result, "");

  END_TEST;
}

// Ensure that Event::Signal while holding a spinlock is safe.
//
// This is a regression test for fxbug.dev/77392.
static bool event_signal_spinlock_test() {
  BEGIN_TEST;

  struct Args {
    RelaxedAtomic<bool> about_to_wait{false};
    Event event;
  };

  thread_start_routine Waiter = [](void* args_) -> int {
    auto* args = reinterpret_cast<Args*>(args_);
    args->about_to_wait.store(true);
    args->event.Wait();
    return 0;
  };

  // Pin the current thread to its CPU.
  Thread* const current_thread = Thread::Current::Get();
  const cpu_mask_t original_affinity_mask = current_thread->GetCpuAffinity();
  const auto restore_affinity = fit::defer([original_affinity_mask, current_thread]() {
    current_thread->SetCpuAffinity(original_affinity_mask);
  });
  cpu_num_t target_cpu = arch_curr_cpu_num();
  current_thread->SetCpuAffinity(cpu_num_to_mask(target_cpu));

  // Create a thread that can only run on this same CPU.
  Args args;
  Thread* t = Thread::Create("event_signal_spinlock_test", Waiter, &args, DEFAULT_PRIORITY);
  t->SetCpuAffinity(cpu_num_to_mask(target_cpu));

  // Give the thread deadline parameters with 100% utilization to increase the likelihood that it
  // reaches its Event::Wait before the current thread reaches its Event::Signal.
  t->SetDeadline({ZX_USEC(150), ZX_USEC(150), ZX_USEC(150)});
  t->Resume();

  // Spin until we know the Waiter has started running.
  while (!args.about_to_wait.load()) {
    Thread::Current::Yield();
  }

  DECLARE_SINGLETON_SPINLOCK_WITH_TYPE(SpinlockForEventSignalTest, MonitoredSpinLock);
  {
    Guard<MonitoredSpinLock, IrqSave> guard{SpinlockForEventSignalTest::Get(), SOURCE_TAG};
    args.event.Signal();
    // Now that we have signaled, we should see that a preemption is pending on this CPU.
    ASSERT_NE(
        0u, (Thread::Current::preemption_state().preempts_pending() & cpu_num_to_mask(target_cpu)));
  }

  t->Join(nullptr, ZX_TIME_INFINITE);

  END_TEST;
}

UNITTEST_START_TESTCASE(event_tests)
UNITTEST("test signaling event with result before waiting", event_signal_result_before_wait_test)
UNITTEST("test signaling event with result after waiting", event_signal_result_after_wait_test)
UNITTEST("test signaling event while holding spinlock", event_signal_spinlock_test)
UNITTEST_END_TESTCASE(event_tests, "event", "Tests for events")
