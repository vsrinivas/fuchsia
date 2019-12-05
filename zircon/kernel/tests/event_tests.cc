// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <kernel/event.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>

// This tests that the result in an event_signal_etc call is propagated to the waiter
// when the event is signaled before any thread waits on the event.
static bool event_signal_result_before_wait_test() {
  BEGIN_TEST;

  event_t event = EVENT_INITIAL_VALUE(event, false, 0);

  zx_status_t signal_result = zx_status_t{42};
  int wake_count = event_signal_etc(&event, true, signal_result);

  ASSERT_EQ(wake_count, 0, "");

  zx_status_t wake_result = event_wait(&event);

  EXPECT_EQ(wake_result, signal_result, "");

  event_destroy(&event);

  END_TEST;
}

struct event_waiter_args {
  event_t* event;
  zx_status_t wake_result;
};

static int event_waiter_thread(void* arg) {
  event_waiter_args* event_args = reinterpret_cast<event_waiter_args*>(arg);
  event_args->wake_result = event_wait(event_args->event);
  return 0;
}

// This tests that the result in an event_signal_etc call is propagated to the waiter
// when the waiter enters a blocking state before the event is signaled.
static bool event_signal_result_after_wait_test() {
  BEGIN_TEST;

  event_t event = EVENT_INITIAL_VALUE(event, false, 0);

  zx_status_t signal_result = zx_status_t{42};

  event_waiter_args args = {&event, ZX_OK};

  thread_t* waiter =
      thread_create("event waiter thread", &event_waiter_thread, &args, DEFAULT_PRIORITY);
  thread_resume(waiter);

  int64_t wait_duration = ZX_USEC(1);
  while (true) {
    {
      // Check if the waiter thread is in the blocked state, indicating that the event
      // has latched.
      Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
      if (waiter->state == THREAD_BLOCKED) {
        break;
      }
    }
    // Nope - sleep and try again.
    thread_sleep_relative(wait_duration);
    wait_duration *= 2;
  }

  int wake_count = event_signal_etc(&event, true, signal_result);
  ASSERT_EQ(wake_count, 1, "expected to wake 1 thread once waiter has latched");

  int thread_retcode = 0;
  thread_join(waiter, &thread_retcode, ZX_TIME_INFINITE);

  ASSERT_EQ(thread_retcode, 0, "");

  event_destroy(&event);

  EXPECT_EQ(args.wake_result, signal_result, "");

  END_TEST;
}

UNITTEST_START_TESTCASE(event_tests)
UNITTEST("test signaling event with result before waiting", event_signal_result_before_wait_test)
UNITTEST("test signaling event with result after waiting", event_signal_result_after_wait_test)
UNITTEST_END_TESTCASE(event_tests, "event", "Tests for events")
