// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <platform.h>

#include <kernel/semaphore.h>
#include <kernel/thread_lock.h>
#include <lockdep/lockdep.h>

using lockdep::Guard;
// using lockdep::ThreadLock;

static bool smoke_test() {
  BEGIN_TEST;

  { Semaphore sema(0); }

  { Semaphore sema(5); }

  { Semaphore sema(-5); }

  {
    Semaphore sema(0);
    ASSERT_EQ(1, sema.Post());
    ASSERT_EQ(2, sema.Post());
    ASSERT_EQ(ZX_OK, sema.Wait(Deadline::infinite()));
    ASSERT_EQ(ZX_OK, sema.Wait(Deadline::infinite()));
    ASSERT_EQ(1, sema.Post());
  }

  END_TEST;
}

static bool timeout_test() {
  BEGIN_TEST;

  auto dealine = Deadline::no_slack(current_time() + ZX_USEC(10));

  Semaphore sema;
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sema.Wait(dealine));
  ASSERT_EQ(1, sema.Post());

  END_TEST;
}

static int wait_sema_thread(void* arg) {
  auto sema = reinterpret_cast<Semaphore*>(arg);
  auto status = sema->Wait(Deadline::infinite());
  return static_cast<int>(status);
}

static bool thread_is_blocked(const thread_t* t) {
  Guard<spin_lock_t, IrqSave> guard{ThreadLock::Get()};
  return (t->state == THREAD_BLOCKED);
}

template <int signal>
static bool signal_test() {
  BEGIN_TEST;

  Semaphore sema;

  auto thread = thread_create("test semaphore", wait_sema_thread, &sema, DEFAULT_PRIORITY);

  ASSERT(nullptr != thread);
  thread_resume(thread);

  while (thread_is_blocked(thread) == false) {
    thread_sleep_relative(ZX_MSEC(1));
  }

  int expected_error = 0;
  if constexpr (signal == 1) {
    thread_kill(thread);
    expected_error = ZX_ERR_INTERNAL_INTR_KILLED;
  }
  if constexpr (signal == 2) {
    thread_suspend(thread);
    expected_error = ZX_ERR_INTERNAL_INTR_RETRY;
  }

  int retcode = ZX_OK;
  thread_join(thread, &retcode, ZX_TIME_INFINITE);
  ASSERT_EQ(expected_error, retcode);
  ASSERT_EQ(1, sema.Post());

  END_TEST;
}

UNITTEST_START_TESTCASE(semaphore_tests)
UNITTEST("smoke_test", smoke_test)
UNITTEST("timeout_test", timeout_test)
UNITTEST("kill_signal_test", signal_test<1>)
UNITTEST("suspend_signal_test", signal_test<2>)
UNITTEST_END_TESTCASE(semaphore_tests, "semaphore", "Semaphore tests")
