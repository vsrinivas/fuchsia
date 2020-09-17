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

static bool smoke_test() {
  BEGIN_TEST;

  {
    Semaphore sema;
    ASSERT_EQ(0u, sema.count());
    ASSERT_EQ(0u, sema.num_waiters());
  }

  {
    Semaphore sema(0);
    ASSERT_EQ(0u, sema.count());
    ASSERT_EQ(0u, sema.num_waiters());
  }

  {
    Semaphore sema(5);
    ASSERT_EQ(5u, sema.count());
    ASSERT_EQ(0u, sema.num_waiters());
  }

  {
    constexpr uint64_t kPostCount = 10;
    Semaphore sema;

    for (uint64_t i = 0; i < kPostCount; ++i) {
      ASSERT_EQ(i, sema.count());
      ASSERT_EQ(0u, sema.num_waiters());

      sema.Post();

      ASSERT_EQ(i + 1, sema.count());
      ASSERT_EQ(0u, sema.num_waiters());
    }

    for (uint64_t i = 0; i < kPostCount; ++i) {
      ASSERT_EQ(kPostCount - i, sema.count());
      ASSERT_EQ(0u, sema.num_waiters());

      ASSERT_EQ(ZX_OK, sema.Wait(Deadline::infinite()));

      ASSERT_EQ(kPostCount - i - 1, sema.count());
      ASSERT_EQ(0u, sema.num_waiters());
    }
  }

  END_TEST;
}

static bool timeout_test() {
  BEGIN_TEST;

  auto dealine = Deadline::after(ZX_USEC(10));

  Semaphore sema;
  ASSERT_EQ(0u, sema.count());
  ASSERT_EQ(0u, sema.num_waiters());
  ASSERT_EQ(ZX_ERR_TIMED_OUT, sema.Wait(dealine));
  ASSERT_EQ(0u, sema.count());
  ASSERT_EQ(0u, sema.num_waiters());

  END_TEST;
}

static int wait_sema_thread(void* arg) {
  auto sema = reinterpret_cast<Semaphore*>(arg);
  auto status = sema->Wait(Deadline::infinite());
  return static_cast<int>(status);
}

static bool thread_is_blocked(const Thread* t) {
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  return (t->state() == THREAD_BLOCKED);
}

enum class Signal { kPost, kKill, kSuspend };

template <Signal signal>
static bool signal_test() {
  BEGIN_TEST;

  Semaphore sema;

  ASSERT_EQ(0u, sema.count());
  ASSERT_EQ(0u, sema.num_waiters());

  auto thread = Thread::Create("test semaphore", wait_sema_thread, &sema, DEFAULT_PRIORITY);

  ASSERT(nullptr != thread);
  thread->Resume();

  while (thread_is_blocked(thread) == false) {
    Thread::Current::SleepRelative(ZX_MSEC(1));
  }

  ASSERT_EQ(0u, sema.count());
  ASSERT_EQ(1u, sema.num_waiters());

  int expected_error;
  switch (signal) {
    case Signal::kPost:
      sema.Post();
      expected_error = ZX_OK;
      break;

    case Signal::kKill:
      thread->Kill();
      expected_error = ZX_ERR_INTERNAL_INTR_KILLED;
      break;

    case Signal::kSuspend:
      thread->Suspend();
      expected_error = ZX_ERR_INTERNAL_INTR_RETRY;
      break;
  }

  int retcode = ZX_OK;
  thread->Join(&retcode, ZX_TIME_INFINITE);
  ASSERT_EQ(expected_error, retcode);

  ASSERT_EQ(0u, sema.count());
  ASSERT_EQ(0u, sema.num_waiters());

  END_TEST;
}

UNITTEST_START_TESTCASE(semaphore_tests)
UNITTEST("smoke_test", smoke_test)
UNITTEST("timeout_test", timeout_test)
UNITTEST("post_signal_test", signal_test<Signal::kPost>)
UNITTEST("kill_signal_test", signal_test<Signal::kKill>)
UNITTEST("suspend_signal_test", signal_test<Signal::kSuspend>)
UNITTEST_END_TESTCASE(semaphore_tests, "semaphore", "Semaphore tests")
