// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include <kernel/spinlock.h>

namespace {

bool spinlock_lock_unlock() {
  BEGIN_TEST;

  SpinLock spinlock;
  spin_lock_saved_state_t state;

  spinlock.AcquireIrqSave(state);
  spinlock.ReleaseIrqRestore(state);

  spinlock.AcquireIrqSave(state);
  spinlock.ReleaseIrqRestore(state);

  END_TEST;
}

bool spinlock_is_held() {
  BEGIN_TEST;

  SpinLock spinlock;
  spin_lock_saved_state_t state;

  EXPECT_FALSE(spinlock.IsHeld(), "Lock not held");
  spinlock.AcquireIrqSave(state);
  EXPECT_TRUE(spinlock.IsHeld(), "Lock held");
  spinlock.ReleaseIrqRestore(state);
  EXPECT_FALSE(spinlock.IsHeld(), "Lock not held");

  END_TEST;
}

bool spinlock_assert_held() {
  BEGIN_TEST;

  SpinLock spinlock;
  spin_lock_saved_state_t state;

  spinlock.AcquireIrqSave(state);
  spinlock.AssertHeld();  // Lock is held: this should be a no-op.
  spinlock.ReleaseIrqRestore(state);

  END_TEST;
}

// A struct with a guarded value.
struct ObjectWithLock {
  SpinLock lock;
  int val TA_GUARDED(lock);
  spin_lock_saved_state_t state;

  void TakeLock() TA_NO_THREAD_SAFETY_ANALYSIS { lock.AcquireIrqSave(state); }
};

bool spinlock_assert_held_compile_test() {
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
  object.lock.AssertHeld();
#endif
  object.val = 3;

  // Without the assertion, Clang will object to releasing the lock.
  object.lock.ReleaseIrqRestore(object.state);

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(spinlock_tests)
UNITTEST("spinlock_lock_unlock", spinlock_lock_unlock)
UNITTEST("spinlock_is_held", spinlock_is_held)
UNITTEST("spinlock_assert_held", spinlock_assert_held)
UNITTEST("spinlock_assert_held_compile_test", spinlock_assert_held_compile_test)
UNITTEST_END_TESTCASE(spinlock_tests, "spinlock", "SpinLock tests")
