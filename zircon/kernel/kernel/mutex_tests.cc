// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <kernel/mutex.h>

namespace {

bool mutex_lock_unlock() {
  BEGIN_TEST;

  Mutex mutex;

  mutex.Acquire();
  mutex.Release();

  mutex.Acquire();
  mutex.Release();

  END_TEST;
}

bool mutex_is_held() {
  BEGIN_TEST;

  Mutex mutex;

  EXPECT_FALSE(mutex.IsHeld(), "Lock not held");
  mutex.Acquire();
  EXPECT_TRUE(mutex.IsHeld(), "Lock held");
  mutex.Release();
  EXPECT_FALSE(mutex.IsHeld(), "Lock not held");

  END_TEST;
}

bool mutex_assert_held() {
  BEGIN_TEST;

  Mutex mutex;

  mutex.Acquire();
  mutex.AssertHeld();  // Lock is held: this should be a no-op.
  mutex.Release();

  END_TEST;
}

// A struct with a guarded value.
struct ObjectWithLock {
  Mutex mu;
  int val TA_GUARDED(mu);

  void TakeLock() TA_NO_THREAD_SAFETY_ANALYSIS { mu.Acquire(); }
};

bool mutex_assert_held_compile_test() {
  BEGIN_TEST;

  ObjectWithLock object;

  // This shouldn't compile with thread analysis enabled.
#if defined(ENABLE_ERRORS)
  object.val = 3;
#endif

  // We take the lock, but Clang can't see it.
  object.TakeLock();

  // Without the assertion, Clang will object to setting "val".
#if !defined(ENABLE_ERRORS)
  object.mu.AssertHeld();
#endif
  object.val = 3;

  // Without the assertion, Clang will object to releasing the lock.
  object.mu.Release();

  END_TEST;
}

DECLARE_SINGLETON_MUTEX(TestSingletonMutex);

// Ensure that acquiring a singleton mutex is thread-safe the first time it is
// acquired.
//
// We've previously had bugs where singleton mutexes were defined static and
// lazily initialised. While in general C++ guarantees that static variables
// are initialised in a thread-safe manner, the kernel turns off those
// mechanisms with the compiler flag "-fno-threadsafe-statics". This led to
// a bug where the first time a mutex was acquired, it could be help by
// multiple threads simultaneously.
//
// This test sets up N threads and races them acquiring the singleton mutex
// "TestSingletonMutex". While the test is safe to run multiple times, it
// can only exercise the static initialisation code path once per boot.
bool singleton_mutex_threadsafe() {
  BEGIN_TEST;

  // If we have already run print a warning that this test is unlikely to exercise anything.
  static bool already_run = false;
  if (already_run) {
    dprintf(INFO,
            "Test has already run this boot. "
            "Subsequent runs will not exercise the mutex init code path again.\n");
  }
  already_run = true;

  // Start multiple threads, all attempting to race to acquire the singleton mutex.
  struct ThreadState {
    ktl::atomic<bool> ready;
    ktl::atomic<bool>* should_start;
    ktl::atomic<bool>* in_critical_section;
    Thread* thread;
  };
  auto worker_body = +[](void* arg) -> int {
    ThreadState* state = static_cast<ThreadState*>(arg);

    // Tell parent we are ready.
    state->ready = true;

    // Spin until all threads are ready to start.
    //
    // We busy-wait here without yielding to try and synchronise threads on
    // different CPUs as much as possible, so that they all race to acquire
    // the mutex below.
    while (!state->should_start->load(ktl::memory_order_relaxed)) {
    }

    {
      // Acquire the mutex.
      Guard<Mutex> guard(TestSingletonMutex::Get());

      // Ensure no other thread already has the mutex.
      bool other_thread_in_critical_section = state->in_critical_section->exchange(true);
      ZX_ASSERT_MSG(!other_thread_in_critical_section,
                    "Another thread was already in the critical section.");

      // Delay before releasing the mutex, to give other threads a chance to
      // notice we are holding it.
      Thread::Current::SleepRelative(ZX_MSEC(1));

      bool still_hold_critical_section = state->in_critical_section->exchange(false);
      ZX_ASSERT_MSG(still_hold_critical_section, "Another thread released our critical section.");
    }

    return 0;
  };

  // Create worker theads and start them up.
  constexpr int kNumThreads = 4;
  std::array<ThreadState, kNumThreads> threads;
  ktl::atomic<bool> should_start = false;
  ktl::atomic<bool> in_critical_section = false;
  for (ThreadState& state : threads) {
    state.ready = false, state.should_start = &should_start,
    state.in_critical_section = &in_critical_section,
    state.thread = Thread::Create("test_singleton_mutex", worker_body, &state, DEFAULT_PRIORITY);
    ASSERT_NONNULL(state.thread, "Thread::Create failed.");
    state.thread->Resume();
  }

  // Wait for all the threads to start.
  for (ThreadState& state : threads) {
    while (!state.ready) {
      Thread::Current::Yield();
    }
  }

  // Let all the threads race.
  should_start = true;

  // Wait for all the threads to finish.
  for (ThreadState& state : threads) {
    int ret;
    state.thread->Join(&ret, ZX_TIME_INFINITE);
  }

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(mutex_tests)
UNITTEST("mutex_lock_unlock", mutex_lock_unlock)
UNITTEST("mutex_is_held", mutex_is_held)
UNITTEST("mutex_assert_held", mutex_assert_held)
UNITTEST("mutex_assert_held_compile_test", mutex_assert_held_compile_test)
UNITTEST("singleton mutex has thread-safe init", singleton_mutex_threadsafe)
UNITTEST_END_TESTCASE(mutex_tests, "mutex", "Mutex tests")
