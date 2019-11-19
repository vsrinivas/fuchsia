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

}  // namespace

UNITTEST_START_TESTCASE(mutex_tests)
UNITTEST("mutex_lock_unlock", mutex_lock_unlock)
UNITTEST("mutex_is_held", mutex_is_held)
UNITTEST("mutex_assert_held", mutex_assert_held)
UNITTEST("mutex_assert_held_compile_test", mutex_assert_held_compile_test)
UNITTEST_END_TESTCASE(mutex_tests, "mutex", "Mutex tests")
