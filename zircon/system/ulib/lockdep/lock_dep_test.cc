// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon-internal/thread_annotations.h>
#include <lockdep/lockdep.h>
#include <zxtest/zxtest.h>

#include "lockdep/lock_traits.h"

// If enabled, introduce locking errors into the tests that we expect Clang
// to detect.
#define TEST_CLANG_DETECTS_LOCK_MISUSE 0

namespace {

using lockdep::AssertHeld;
using lockdep::Guard;

// A custom Mutex implementation.
struct __TA_CAPABILITY("mutex") FakeMutex {
  FakeMutex() = default;

  // No copy or move.
  FakeMutex(FakeMutex&&) = delete;
  FakeMutex(const FakeMutex&) = delete;
  FakeMutex& operator=(FakeMutex&&) = delete;
  FakeMutex& operator=(const FakeMutex&) = delete;

  // Locking operations.
  void Acquire() __TA_ACQUIRE() { acquired = true; }
  void Release() __TA_RELEASE() { acquired = false; }
  void AssertHeld() const __TA_ASSERT() { assert_held_called = true; }

  bool acquired = false;
  mutable bool assert_held_called = false;
};
LOCK_DEP_TRAITS(FakeMutex, lockdep::LockFlagsNone);

// Take/release locks in a way that Clang's lock analysis can't see.
template <typename Lock>
void SecretlyTakeLock(Lock* lock) __TA_NO_THREAD_SAFETY_ANALYSIS {
  lock->lock().Acquire();
}
template <typename Lock>
void SecretlyReleaseLock(Lock* lock) __TA_NO_THREAD_SAFETY_ANALYSIS {
  lock->lock().Release();
}

LOCK_DEP_SINGLETON_LOCK(SingletonLock, FakeMutex);

TEST(LockDep, SingletonLockGuard) {
  static int guarded_var TA_GUARDED(SingletonLock::Get()) = 0;
  EXPECT_FALSE(SingletonLock::Get()->lock().acquired);

  // Take the lock, and ensure it was actually acquired.
  Guard<FakeMutex> guard{SingletonLock::Get()};
  EXPECT_TRUE(SingletonLock::Get()->lock().acquired);

  // Access the locked variable. Clang should not complain.
  guarded_var++;

  // Release the lock.
  guard.Release();
  EXPECT_FALSE(SingletonLock::Get()->lock().acquired);

  // Access the locked variable. Clang should fail compilation.
#if 0 || TEST_CLANG_DETECTS_LOCK_MISUSE
  guarded_var++;
#endif
}

TEST(LockDep, SingletonLockAssertHeld) {
  static int guarded_var TA_GUARDED(SingletonLock::Get()) = 0;
  EXPECT_FALSE(SingletonLock::Get()->lock().acquired);

  // Take the lock in a way the Clang can't detect.
  SecretlyTakeLock(SingletonLock::Get());
  EXPECT_TRUE(SingletonLock::Get()->lock().acquired);

  // Call AssertHeld() on the lock. Clang should be satisifed we have
  // the lock, and let us modify the guarded field.
  SingletonLock::Get()->lock().assert_held_called = false;
  AssertHeld(*SingletonLock::Get());
  EXPECT_TRUE(SingletonLock::Get()->lock().assert_held_called);
  guarded_var++;

  // Release the lock.
  SecretlyReleaseLock(SingletonLock::Get());
}

// A wrapped external lock.
FakeMutex global_lock;
LOCK_DEP_SINGLETON_LOCK_WRAPPER(WrappedGlobalLock, global_lock);

TEST(LockDep, WrappedGlobalLockGuard) {
  static int guarded_var TA_GUARDED(WrappedGlobalLock::Get()) = 0;
  static int guarded_raw_var TA_GUARDED(global_lock) = 0;
  EXPECT_FALSE(WrappedGlobalLock::Get()->lock().acquired);

  // Take the lock, and ensure it was actually acquired.
  Guard<FakeMutex> guard{WrappedGlobalLock::Get()};
  EXPECT_TRUE(WrappedGlobalLock::Get()->lock().acquired);

  // Access the locked variable. Clang should not complain.
  guarded_var++;
  guarded_raw_var++;

  // Release the lock.
  guard.Release();
  EXPECT_FALSE(WrappedGlobalLock::Get()->lock().acquired);

  // Access the locked variable. Clang should fail compilation.
#if 0 || TEST_CLANG_DETECTS_LOCK_MISUSE
  guarded_var++;
  guarded_raw_var++;
#endif
}

TEST(LockDep, WrappedGlobalLockAssertHeld) {
  static int guarded_var TA_GUARDED(WrappedGlobalLock::Get()) = 0;
  static int guarded_raw_var TA_GUARDED(global_lock) = 0;
  EXPECT_FALSE(WrappedGlobalLock::Get()->lock().acquired);

  // Take the lock in a way the Clang can't detect.
  SecretlyTakeLock(WrappedGlobalLock::Get());
  EXPECT_TRUE(WrappedGlobalLock::Get()->lock().acquired);

  // Call AssertHeld() on the lock. Clang should be satisifed we have
  // the lock, and let us modify the guarded field.
  WrappedGlobalLock::Get()->lock().assert_held_called = false;
  AssertHeld(*WrappedGlobalLock::Get());
  EXPECT_TRUE(WrappedGlobalLock::Get()->lock().assert_held_called);
  guarded_var++;
  guarded_raw_var++;

  // Release the lock.
  SecretlyReleaseLock(WrappedGlobalLock::Get());
}

// An object using FakeMutex.
struct FakeLockable {
  int guarded_field __TA_GUARDED(lock) = 0;
  LOCK_DEP_INSTRUMENT(FakeLockable, FakeMutex) lock;
};

TEST(LockDep, LockableObjectLockGuard) {
  FakeLockable lockable;
  EXPECT_FALSE(lockable.lock.lock().acquired);

  // Take the lock, and ensure it was actually acquired.
  Guard<FakeMutex> guard{&lockable.lock};
  EXPECT_TRUE(lockable.lock.lock().acquired);

  // Access the locked variable. Clang should not complain.
  lockable.guarded_field++;

  // Release the lock.
  guard.Release();
  EXPECT_FALSE(lockable.lock.lock().acquired);

  // Access the locked variable. Clang should fail compilation.
#if 0 || TEST_CLANG_DETECTS_LOCK_MISUSE
  lockable.guarded_field++;
#endif
}

TEST(LockDep, LockableObjectLockAssertHeld) {
  FakeLockable lockable;
  EXPECT_FALSE(lockable.lock.lock().acquired);

  // Take the lock in a way the Clang can't detect.
  SecretlyTakeLock(&lockable.lock);
  EXPECT_TRUE(lockable.lock.lock().acquired);

  // Call AssertHeld() on the lock. Clang should be satisifed we have
  // the lock, and let us modify the guarded field.
  lockable.lock.lock().assert_held_called = false;
  AssertHeld(lockable.lock);
  EXPECT_TRUE(lockable.lock.lock().assert_held_called);
  lockable.guarded_field++;

  // Release the lock.
  SecretlyReleaseLock(&lockable.lock);
}

}  // namespace
