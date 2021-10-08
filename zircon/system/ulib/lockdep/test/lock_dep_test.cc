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
struct GetTlsTestState {
  static void Reset(lockdep::LockFlags expected) {
    expected_flags = expected;
    match_count = 0;
    mismatch_count = 0;
  }
  static inline lockdep::LockFlags expected_flags{lockdep::LockFlagsNone};
  static inline uint32_t match_count{0};
  static inline uint32_t mismatch_count{0};
};
}  // namespace

// Implementation of the user supplied runtime API.
namespace lockdep {
// Override the weak implementation of SystemGetThreadLockState so we can test
// to be sure that LockFlags are properly propagated to the implementation.
ThreadLockState* SystemGetThreadLockState(LockFlags lock_flags) {
  thread_local ThreadLockState thread_lock_state{};

  if (lock_flags == GetTlsTestState::expected_flags) {
    ++GetTlsTestState::match_count;
  } else {
    ++GetTlsTestState::mismatch_count;
  }

  return &thread_lock_state;
}
void SystemLockValidationError(AcquiredLockEntry* lock_entry, AcquiredLockEntry* conflicting_entry,
                               ThreadLockState* state, void* caller_address, void* caller_frame,
                               LockResult result) {
  ZX_DEBUG_ASSERT(false);
}
void SystemLockValidationFatal(AcquiredLockEntry* lock_entry, ThreadLockState* state,
                               void* caller_address, void* caller_frame, LockResult result) {
  ZX_DEBUG_ASSERT(false);
}
void SystemCircularLockDependencyDetected(LockClassState* connected_set_root) {
  ZX_DEBUG_ASSERT(false);
}
void SystemInitThreadLockState(ThreadLockState* state) { ZX_DEBUG_ASSERT(false); }
void SystemTriggerLoopDetection() { ZX_DEBUG_ASSERT(false); }
}  // namespace lockdep

namespace {

using lockdep::AdoptLock;
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

TEST(LockDep, GuardMoveSemantics) {
  Guard<FakeMutex> guard{SingletonLock::Get()};
  EXPECT_TRUE(guard);
  EXPECT_TRUE(SingletonLock::Get()->lock().acquired);

  Guard<FakeMutex> guard2{AdoptLock, guard.take()};
  EXPECT_FALSE(guard);
  EXPECT_TRUE(guard2);
  EXPECT_TRUE(SingletonLock::Get()->lock().acquired);

  guard2.Release();
  EXPECT_FALSE(guard);
  EXPECT_FALSE(guard2);
  EXPECT_FALSE(SingletonLock::Get()->lock().acquired);
}

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
  lockdep::Lock<FakeMutex>* get_lock() __TA_RETURN_CAPABILITY(lock) { return &lock; }
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

// TODO(33187): Enable this test when lockdep has a userspace runtime and
// validation can be tested in userspace.
#if false && ZX_DEBUG_ASSERT_IMPLEMENTED
TEST(LockDep, ZxDebugAssertOnNonNestableLock) {
  // Verify the tagged constructor asserts in debug builds.
  ASSERT_DEATH(([] {
    FakeLockable lockable;
    Guard<FakeMutex> guard{lockdep::AssertOrderedLock, lockable.get_lock(), 0};
  }));
}
#endif

struct __TA_CAPABILITY("mutex") FakeMutexWithPolicyAndFlags {
  static constexpr lockdep::LockFlags kFlags =
      lockdep::LockFlagsIrqSafe | lockdep::LockFlagsMultiAcquire;

  FakeMutexWithPolicyAndFlags() = default;

  // No copy or move.
  FakeMutexWithPolicyAndFlags(FakeMutexWithPolicyAndFlags&&) = delete;
  FakeMutexWithPolicyAndFlags(const FakeMutexWithPolicyAndFlags&) = delete;
  FakeMutexWithPolicyAndFlags& operator=(FakeMutexWithPolicyAndFlags&&) = delete;
  FakeMutexWithPolicyAndFlags& operator=(const FakeMutexWithPolicyAndFlags&) = delete;

  // Locking operations.
  void Acquire() __TA_ACQUIRE() {}
  void Release() __TA_RELEASE() {}
  void AssertHeld() const __TA_ASSERT() {}
};

LOCK_DEP_TRAITS(FakeMutexWithPolicyAndFlags, FakeMutexWithPolicyAndFlags::kFlags);

struct FakeMutexPolicy {
  static constexpr uint32_t kOrderSentinel = 0;
  struct StageInfo {
    uint32_t call_count{0};
    uint32_t last_called_order{kOrderSentinel};
    static inline uint32_t stage_order{0};

    void RecordCalled() {
      ++call_count;
      last_called_order = ++stage_order;
    }
  };

  struct State {};

  template <typename LockType>
  static void PreValidate(LockType* lock, State*) {
    stage_info[0].RecordCalled();
  }

  template <typename LockType>
  static bool Acquire(LockType* lock, State*) __TA_ACQUIRE(lock) {
    stage_info[1].RecordCalled();
    lock->Acquire();
    return !force_acquire_failure;
  }

  template <typename LockType>
  static void Release(LockType* lock, State*) __TA_RELEASE(lock) {
    stage_info[2].RecordCalled();
    lock->Release();
  }

  template <typename LockType>
  static void AssertHeld(const LockType& lock) TA_ASSERT(lock) {
    lock.AssertHeld();
  }

  static void ResetStageInfo() {
    for (StageInfo& info : stage_info) {
      info = StageInfo{};
    }
  }

  static inline bool force_acquire_failure{false};
  static std::array<StageInfo, 3> stage_info;
};

std::array<FakeMutexPolicy::StageInfo, 3> FakeMutexPolicy::stage_info;

LOCK_DEP_POLICY(FakeMutexWithPolicyAndFlags, FakeMutexPolicy);

TEST(LockDep, PolicyOrderFollowed) {
  struct Container {
    LOCK_DEP_INSTRUMENT(Container, FakeMutexWithPolicyAndFlags) lock;
  } container;

  // Start with a typical acquire/release cycle.  Make sure that the hooks are
  // all called exactly once, and in the proper order.
  using FMP = FakeMutexPolicy;
  FMP::ResetStageInfo();
  FMP::force_acquire_failure = false;
  for (const auto& info : FMP::stage_info) {
    EXPECT_EQ(0, info.call_count);
    EXPECT_EQ(FMP::kOrderSentinel, info.last_called_order);
  }

  {
    // Construct a guard and obtain the lock.  Then verify that the pre/post acquire
    // methods were called exactly once, and in the proper order.
    Guard<FakeMutexWithPolicyAndFlags> guard(&container.lock);
    EXPECT_EQ(1, FMP::stage_info[0].call_count);
    EXPECT_EQ(1, FMP::stage_info[1].call_count);
    EXPECT_EQ(0, FMP::stage_info[2].call_count);
    EXPECT_NE(FMP::kOrderSentinel, FMP::stage_info[0].last_called_order);
    EXPECT_EQ(FMP::stage_info[0].last_called_order + 1, FMP::stage_info[1].last_called_order);
    EXPECT_EQ(FMP::kOrderSentinel, FMP::stage_info[2].last_called_order);
  }

  // Now that we have dropped the guard, make sure that the FMP::pre/post release
  // methods were called exactly once, and in the proper order.
  for (const auto& info : FMP::stage_info) {
    EXPECT_EQ(1, info.call_count);
  }
  EXPECT_NE(FMP::kOrderSentinel, FMP::stage_info[0].last_called_order);
  EXPECT_EQ(FMP::stage_info[0].last_called_order + 1, FMP::stage_info[1].last_called_order);
  EXPECT_EQ(FMP::stage_info[1].last_called_order + 1, FMP::stage_info[2].last_called_order);

  // Repeat the test, but this time, force the acquire to fail.  Things should
  // behave the same way, except that the Release policy method should not be
  // called.
  FMP::ResetStageInfo();
  FMP::force_acquire_failure = true;
  for (const auto& info : FMP::stage_info) {
    EXPECT_EQ(0, info.call_count);
    EXPECT_EQ(FMP::kOrderSentinel, info.last_called_order);
  }

  {
    // Construct a guard and obtain the lock.  Then verify that the pre/post acquire
    // methods were called exactly once, and in the proper order.
    Guard<FakeMutexWithPolicyAndFlags> guard(&container.lock);
    EXPECT_EQ(1, FMP::stage_info[0].call_count);
    EXPECT_EQ(1, FMP::stage_info[1].call_count);
    EXPECT_EQ(0, FMP::stage_info[2].call_count);
    EXPECT_NE(FMP::kOrderSentinel, FMP::stage_info[0].last_called_order);
    EXPECT_EQ(FMP::stage_info[0].last_called_order + 1, FMP::stage_info[1].last_called_order);
    EXPECT_EQ(FMP::kOrderSentinel, FMP::stage_info[2].last_called_order);
  }

  // Now that we have dropped the guard, make sure that the FMP::pre/post release
  // methods were called exactly once, and in the proper order.
  EXPECT_EQ(1, FMP::stage_info[0].call_count);
  EXPECT_EQ(1, FMP::stage_info[1].call_count);
  EXPECT_EQ(0, FMP::stage_info[2].call_count);
  EXPECT_NE(FMP::kOrderSentinel, FMP::stage_info[0].last_called_order);
  EXPECT_EQ(FMP::stage_info[0].last_called_order + 1, FMP::stage_info[1].last_called_order);
  EXPECT_EQ(FMP::kOrderSentinel, FMP::stage_info[2].last_called_order);
}

TEST(LockDep, FlagsPassedToSystemGetThreadLockState) {
  // Check to make sure that the LockFlags associated with each lock type are
  // properly passed to the SystemGetThreadLockState runtime API supplied by the
  // user.

  // Start with a lock with no flags associated with it.
  {
    struct Container {
      LOCK_DEP_INSTRUMENT(Container, FakeMutex) lock;
    } container;

    GetTlsTestState::Reset(lockdep::LockFlagsNone);
    EXPECT_EQ(0, GetTlsTestState::match_count);
    EXPECT_EQ(0, GetTlsTestState::mismatch_count);

    { Guard<FakeMutex> guard(&container.lock); }

    EXPECT_GT(GetTlsTestState::match_count, 0);
    EXPECT_EQ(0, GetTlsTestState::mismatch_count);
  }

  // Repeat the test, this time with a lock type which does have flags set.
  {
    struct Container {
      LOCK_DEP_INSTRUMENT(Container, FakeMutexWithPolicyAndFlags) lock;
    } container;

    GetTlsTestState::Reset(FakeMutexWithPolicyAndFlags::kFlags);
    EXPECT_EQ(0, GetTlsTestState::match_count);
    EXPECT_EQ(0, GetTlsTestState::mismatch_count);

    { Guard<FakeMutexWithPolicyAndFlags> guard(&container.lock); }

    EXPECT_GT(GetTlsTestState::match_count, 0);
    EXPECT_EQ(0, GetTlsTestState::mismatch_count);
  }
}

}  // namespace
