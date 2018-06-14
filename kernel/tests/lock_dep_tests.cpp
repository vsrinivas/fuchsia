// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <stdint.h>
#include <fbl/mutex.h>
#include <lib/unittest/unittest.h>
#include <lockdep/guard_multiple.h>
#include <lockdep/lockdep.h>

#if WITH_LOCK_DEP_TESTS

namespace test {

// Global flag that determines whether try lock operations succeed.
bool g_try_lock_succeeds = true;

// Define some proxy types to simulate different kinds of locks.
struct Spinlock : fbl::Mutex {
    using fbl::Mutex::Mutex;

    bool AcquireIrqSave(uint64_t* flags) __TA_ACQUIRE() {
        (void)flags;
        Acquire();
        return true;
    }
    void ReleaseIrqRestore(uint64_t flags) __TA_RELEASE() {
        (void)flags;
        Release();
    }

    bool TryAcquire() __TA_TRY_ACQUIRE(true) {
        if (g_try_lock_succeeds)
            Acquire();
        return g_try_lock_succeeds;
    }
    bool TryAcquireIrqSave(uint64_t* flags) __TA_TRY_ACQUIRE(true) {
        (void) flags;
        if (g_try_lock_succeeds)
            Acquire();
        return g_try_lock_succeeds;
    }
};
LOCK_DEP_TRAITS(Spinlock, lockdep::LockFlagsIrqSafe);

// Fake C-style locking primitive.
struct spinlock_t {};
LOCK_DEP_TRAITS(spinlock_t, lockdep::LockFlagsIrqSafe);

void spinlock_lock(spinlock_t* /*lock*/) {}
void spinlock_unlock(spinlock_t* /*lock*/) {}
bool spinlock_try_lock(spinlock_t* /*lock*/) { return true; }
void spinlock_lock_irqsave(spinlock_t* /*lock*/, uint64_t* /*flags*/) {}
void spinlock_unlock_irqrestore(spinlock_t* /*lock*/, uint64_t /*flags*/) {}
bool spinlock_try_lock_irqsave(spinlock_t* /*lock*/, uint64_t* /*flags*/) { return true; }

// Type tags to select Guard<> lock policies for Spinlock and spinlock_t.
struct IrqSave {};
struct NoIrqSave {};
struct TryIrqSave {};
struct TryNoIrqSave {};

struct SpinlockNoIrqSave {
    struct State {};

    static bool Acquire(Spinlock* lock, State*) __TA_ACQUIRE(lock) {
        lock->Acquire();
        return true;
    }
    static void Release(Spinlock* lock, State*) __TA_RELEASE(lock) {
        lock->Release();
    }
};
LOCK_DEP_POLICY_OPTION(Spinlock, NoIrqSave, SpinlockNoIrqSave);

struct SpinlockIrqSave {
    struct State {
        State() {}
        uint64_t flags;
    };

    static bool Acquire(Spinlock* lock, State* state) __TA_ACQUIRE(lock) {
        lock->AcquireIrqSave(&state->flags);
        return true;
    }
    static void Release(Spinlock* lock, State* state) __TA_RELEASE(lock) {
        lock->ReleaseIrqRestore(state->flags);
    }
};
LOCK_DEP_POLICY_OPTION(Spinlock, IrqSave, SpinlockIrqSave);

struct SpinlockTryNoIrqSave {
    struct State {};

    static bool Acquire(Spinlock* lock, State*) __TA_TRY_ACQUIRE(true, lock) {
        return lock->TryAcquire();
    }
    static void Release(Spinlock* lock, State*) __TA_RELEASE(lock) {
        lock->Release();
    }
};
LOCK_DEP_POLICY_OPTION(Spinlock, TryNoIrqSave, SpinlockTryNoIrqSave);

struct SpinlockTryIrqSave {
    struct State {
        State() {}
        uint64_t flags;
    };

    static bool Acquire(Spinlock* lock, State* state) __TA_TRY_ACQUIRE(true, lock) {
        return lock->TryAcquireIrqSave(&state->flags);
    }
    static void Release(Spinlock* lock, State* state) __TA_RELEASE(lock) {
        lock->ReleaseIrqRestore(state->flags);
    }
};
LOCK_DEP_POLICY_OPTION(Spinlock, TryIrqSave, SpinlockTryIrqSave);

struct spinlock_t_NoIrqSave {
    struct State {};

    static bool Acquire(spinlock_t* lock, State*) {
        spinlock_lock(lock);
        return true;
    }
    static void Release(spinlock_t* lock, State*) {
        spinlock_unlock(lock);
    }
};
LOCK_DEP_POLICY_OPTION(spinlock_t, NoIrqSave, spinlock_t_NoIrqSave);

struct spinlock_t_IrqSave {
    struct State {
        State() {}
        uint64_t flags;
    };

    static bool Acquire(spinlock_t* lock, State* state) {
        spinlock_lock_irqsave(lock, &state->flags);
        return true;
    }
    static void Release(spinlock_t* lock, State* state) {
        spinlock_unlock_irqrestore(lock, state->flags);
    }
};
LOCK_DEP_POLICY_OPTION(spinlock_t, IrqSave, spinlock_t_IrqSave);

struct spinlock_t_TryNoIrqSave {
    struct State {};

    static bool Acquire(spinlock_t* lock, State*) {
        spinlock_lock(lock);
      return g_try_lock_succeeds;
    }
    static void Release(spinlock_t* lock, State*) {
        spinlock_unlock(lock);
    }
};
LOCK_DEP_POLICY_OPTION(spinlock_t, TryNoIrqSave, spinlock_t_TryNoIrqSave);

struct spinlock_t_TryIrqSave {
    struct State {
        State() {}
        uint64_t flags;
    };

    static bool Acquire(spinlock_t* lock, State* state) {
        spinlock_lock_irqsave(lock, &state->flags);
      return g_try_lock_succeeds;
    }
    static void Release(spinlock_t* lock, State* state) {
        spinlock_unlock_irqrestore(lock, state->flags);
    }
};
LOCK_DEP_POLICY_OPTION(spinlock_t, TryIrqSave, spinlock_t_TryIrqSave);

struct Mutex : fbl::Mutex {
    using fbl::Mutex::Mutex;
};
// Uses the default traits: fbl::LockClassState::None.

struct Nestable : fbl::Mutex {
    using fbl::Mutex::Mutex;
};
LOCK_DEP_TRAITS(Nestable, lockdep::LockFlagsNestable);

struct Foo {
    LOCK_DEP_INSTRUMENT(Foo, Mutex) lock;

    void TestRequire() __TA_REQUIRES(lock) {}
    void TestExclude() __TA_EXCLUDES(lock) {}
};

struct Bar {
    LOCK_DEP_INSTRUMENT(Bar, Mutex) lock;

    void TestRequire() __TA_REQUIRES(lock) {}
    void TestExclude() __TA_EXCLUDES(lock) {}
};

template <typename LockType>
struct Baz {
    LOCK_DEP_INSTRUMENT(Baz, LockType) lock;

    void TestRequire() __TA_REQUIRES(lock) {}
    void TestExclude() __TA_EXCLUDES(lock) {}
};

struct MultipleLocks {
    LOCK_DEP_INSTRUMENT(MultipleLocks, Mutex) lock_a;
    LOCK_DEP_INSTRUMENT(MultipleLocks, Mutex) lock_b;

    void TestRequireLockA() __TA_REQUIRES(lock_a) {}
    void TestExcludeLockA() __TA_EXCLUDES(lock_a) {}
    void TestRequireLockB() __TA_REQUIRES(lock_b) {}
    void TestExcludeLockB() __TA_EXCLUDES(lock_b) {}
};

template <size_t Index>
struct Number {
    LOCK_DEP_INSTRUMENT(Number, Mutex) lock;

    void TestRequire() __TA_REQUIRES(lock) {}
    void TestExclude() __TA_EXCLUDES(lock) {}
};

lockdep::LockResult GetLastResult() {
#if WITH_LOCK_DEP
    lockdep::ThreadLockState* state = lockdep::ThreadLockState::Get();
    return state->last_result();
#else
    return lockdep::LockResult::Success;
#endif
}

void ResetTrackingState() {
#if WITH_LOCK_DEP
    for (auto& state : lockdep::LockClassState::Iter())
      state.Reset();
#endif
}

} // namespace test

static bool lock_dep_dynamic_analysis_tests() {
    BEGIN_TEST;

    using lockdep::Guard;
    using lockdep::GuardMultiple;
    using lockdep::LockResult;
    using lockdep::ThreadLockState;
    using lockdep::LockClassState;
    using test::Bar;
    using test::Baz;
    using test::Foo;
    using test::GetLastResult;
    using test::IrqSave;
    using test::TryIrqSave;
    using test::MultipleLocks;
    using test::Mutex;
    using test::Nestable;
    using test::NoIrqSave;
    using test::TryNoIrqSave;
    using test::Number;
    using test::Spinlock;
    using test::spinlock_t;

    // Reset the tracking state before each test run.
    test::ResetTrackingState();

    // Single lock.
    {
        Foo a{};

        Guard<Mutex> guard_a{&a.lock};
        EXPECT_TRUE(guard_a, "");
        EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
    }

    // Single lock.
    {
        Bar a{};

        Guard<Mutex> guard_a{&a.lock};
        EXPECT_TRUE(guard_a, "");
        EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
    }

    // Test order invariant.
    {
        Foo a{};
        Foo b{};

        Guard<Mutex> guard_a{&a.lock};
        EXPECT_TRUE(guard_a, "");
        EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

        Guard<Mutex> guard_b{&b.lock};
        EXPECT_TRUE(guard_b, "");
        EXPECT_EQ(LockResult::AlreadyAcquired, test::GetLastResult(), "");
    }

    // Test order invariant with a different lock class.
    {
        Bar a{};
        Bar b{};

        Guard<Mutex> guard_a{&a.lock};
        EXPECT_TRUE(guard_a, "");
        EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

        Guard<Mutex> guard_b{&b.lock};
        EXPECT_TRUE(guard_b, "");
        EXPECT_EQ(LockResult::AlreadyAcquired, test::GetLastResult(), "");
    }

    // Test address order invariant.
    {
        Foo a{};
        Foo b{};

        {
            GuardMultiple<2, Mutex> guard_all{&a.lock, &b.lock};
            EXPECT_TRUE(guard_all, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        {
            GuardMultiple<2, Mutex> guard_all{&b.lock, &a.lock};
            EXPECT_TRUE(guard_all, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }
    }

    // Test address order invariant with a differnt lock class.
    {
        Bar a{};
        Bar b{};

        {
            GuardMultiple<2, Mutex> guard_all{&a.lock, &b.lock};
            EXPECT_TRUE(guard_all, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        {
            GuardMultiple<2, Mutex> guard_all{&b.lock, &a.lock};
            EXPECT_TRUE(guard_all, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }
    }

    // Test address order invariant with spinlocks.
    {
        Baz<Spinlock> a{};
        Baz<Spinlock> b{};

        {
            GuardMultiple<2, Spinlock, NoIrqSave> guard_all{&a.lock, &b.lock};
            EXPECT_TRUE(guard_all, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        {
            GuardMultiple<2, Spinlock, NoIrqSave> guard_all{&b.lock, &a.lock};
            EXPECT_TRUE(guard_all, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        {
            test::g_try_lock_succeeds = true;
            GuardMultiple<2, Spinlock, TryNoIrqSave> guard_all{&a.lock, &b.lock};
            EXPECT_TRUE(guard_all, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        {
            test::g_try_lock_succeeds = true;
            GuardMultiple<2, Spinlock, TryNoIrqSave> guard_all{&b.lock, &a.lock};
            EXPECT_TRUE(guard_all, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        {
            test::g_try_lock_succeeds = false;
            GuardMultiple<2, Spinlock, TryNoIrqSave> guard_all{&a.lock, &b.lock};
            EXPECT_FALSE(guard_all, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        {
            test::g_try_lock_succeeds = false;
            GuardMultiple<2, Spinlock, TryNoIrqSave> guard_all{&b.lock, &a.lock};
            EXPECT_FALSE(guard_all, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }
    }

    // Foo -> Bar -- establish order.
    {
        Foo a{};
        Bar b{};

        Guard<Mutex> guard_a{&a.lock};
        EXPECT_TRUE(guard_a, "");
        EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

        Guard<Mutex> guard_b{&b.lock};
        EXPECT_TRUE(guard_b, "");
        EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
    }

    // Bar -> Foo -- check order invariant.
    {
        Foo a{};
        Bar b{};

        Guard<Mutex> guard_b{&b.lock};
        EXPECT_TRUE(guard_b, "");
        EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

        Guard<Mutex> guard_a{&a.lock};
        EXPECT_TRUE(guard_a, "");
        EXPECT_EQ(LockResult::OutOfOrder, test::GetLastResult(), "");
    }

    // Test external order invariant.
    {
        Baz<Nestable> baz1;
        Baz<Nestable> baz2;

        {
            Guard<Nestable> auto_baz1{&baz1.lock, 0};
            EXPECT_TRUE(auto_baz1, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Nestable> auto_baz2{&baz2.lock, 1};
            EXPECT_TRUE(auto_baz2, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        {
            Guard<Nestable> auto_baz2{&baz2.lock, 0};
            EXPECT_TRUE(auto_baz2, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Nestable> auto_baz1{&baz1.lock, 1};
            EXPECT_TRUE(auto_baz1, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        {
            Guard<Nestable> auto_baz2{&baz2.lock, 1};
            EXPECT_TRUE(auto_baz2, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Nestable> auto_baz1{&baz1.lock, 0};
            EXPECT_TRUE(auto_baz1, "");
            EXPECT_EQ(LockResult::InvalidNesting, test::GetLastResult(), "");
        }
    }

    // Test irq-safety invariant.
    {
        Baz<Mutex> baz1;
        Baz<Spinlock> baz2;

        {
            Guard<Mutex> auto_baz1{&baz1.lock};
            EXPECT_TRUE(auto_baz1, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Spinlock, NoIrqSave> auto_baz2{&baz2.lock};
            EXPECT_TRUE(auto_baz2, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        {
            Guard<Spinlock, NoIrqSave> auto_baz2{&baz2.lock};
            EXPECT_TRUE(auto_baz2, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Mutex> auto_baz1{&baz1.lock};
            EXPECT_TRUE(auto_baz1, "");
            EXPECT_EQ(LockResult::InvalidIrqSafety, test::GetLastResult(), "");
        }
    }

    // Test spinlock options compile and baisc guard functions.
    // TODO(eieio): Add Guard<>::state() accessor and check state values.
    {
        Baz<Spinlock> baz1;
        Baz<spinlock_t> baz2;

        {
            Guard<Spinlock, NoIrqSave> guard{&baz1.lock};
            EXPECT_TRUE(guard, "");
            guard.Release();
            EXPECT_FALSE(guard, "");
        }

        {
            Guard<Spinlock, IrqSave> guard{&baz1.lock};
            EXPECT_TRUE(guard, "");
            guard.Release();
            EXPECT_FALSE(guard, "");
        }

        {
            Guard<spinlock_t, NoIrqSave> guard{&baz2.lock};
            EXPECT_TRUE(guard, "");
            guard.Release();
            EXPECT_FALSE(guard, "");
        }

        {
            Guard<spinlock_t, IrqSave> guard{&baz2.lock};
            EXPECT_TRUE(guard, "");
            guard.Release();
            EXPECT_FALSE(guard, "");
        }

        {
            test::g_try_lock_succeeds = true;
            Guard<Spinlock, TryNoIrqSave> guard{&baz1.lock};
            EXPECT_TRUE(guard, "");
            guard.Release();
            EXPECT_FALSE(guard, "");
        }

        {
            test::g_try_lock_succeeds = true;
            Guard<Spinlock, TryIrqSave> guard{&baz1.lock};
            EXPECT_TRUE(guard, "");
            guard.Release();
            EXPECT_FALSE(guard, "");
        }

        {
            test::g_try_lock_succeeds = false;
            Guard<spinlock_t, TryNoIrqSave> guard{&baz2.lock};
            EXPECT_FALSE(guard, "");
            guard.Release();
            EXPECT_FALSE(guard, "");
        }

        {
            test::g_try_lock_succeeds = false;
            Guard<spinlock_t, TryIrqSave> guard{&baz2.lock};
            EXPECT_FALSE(guard, "");
            guard.Release();
            EXPECT_FALSE(guard, "");
        }

        // Test that Guard<LockType, Option> fails to compile when Option is
        // required by the policy config but not specified.
        {
#if TEST_WILL_NOT_COMPILE || 0
            Guard<Spinlock> guard1{&baz1.lock};
            Guard<spinlock_t> guard2{&baz2.lock};
#endif
        }
    }

    // Test that each lock in a structure behaves as an individual lock class.
    {
        MultipleLocks value{};

        {
            Guard<Mutex> guard_a{&value.lock_a};
            EXPECT_TRUE(guard_a, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Mutex> guard_b{&value.lock_b};
            EXPECT_TRUE(guard_b, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        {
            Guard<Mutex> guard_b{&value.lock_b};
            EXPECT_TRUE(guard_b, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Mutex> guard_a{&value.lock_a};
            EXPECT_TRUE(guard_a, "");
            EXPECT_EQ(LockResult::OutOfOrder, test::GetLastResult(), "");
        }
    }

    // Test circular dependency detection.
    {
        Number<1> a{}; // Node A.
        Number<2> b{}; // Node B.
        Number<3> c{}; // Node C.
        Number<4> d{}; // Node D.

        // A -> B
        {
            Guard<Mutex> guard_a{&a.lock};
            EXPECT_TRUE(guard_a, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Mutex> guard_b{&b.lock};
            EXPECT_TRUE(guard_b, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        // B -> C
        {
            Guard<Mutex> guard_b{&b.lock};
            EXPECT_TRUE(guard_b, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Mutex> guard_c{&c.lock};
            EXPECT_TRUE(guard_c, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        // C -> A -- cycle in (A, B, C)
        {
            Guard<Mutex> guard_c{&c.lock};
            EXPECT_TRUE(guard_c, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Mutex> guard_a{&a.lock};
            EXPECT_TRUE(guard_a, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        // C -> D
        {
            Guard<Mutex> guard_c{&c.lock};
            EXPECT_TRUE(guard_c, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Mutex> guard_d{&d.lock};
            EXPECT_TRUE(guard_d, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }

        // D -> A -- cycle in (A, B, C, D)
        {
            Guard<Mutex> guard_d{&d.lock};
            EXPECT_TRUE(guard_d, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");

            Guard<Mutex> guard_a{&a.lock};
            EXPECT_TRUE(guard_a, "");
            EXPECT_EQ(LockResult::Success, test::GetLastResult(), "");
        }
    }

    END_TEST;
}

// Basic compile-time tests of lockdep clang lock annotations.
static bool lock_dep_static_analysis_tests() {
    BEGIN_TEST;

    using lockdep::Guard;
    using lockdep::GuardMultiple;
    using lockdep::LockResult;
    using lockdep::ThreadLockState;
    using test::Bar;
    using test::Baz;
    using test::Foo;
    using test::MultipleLocks;
    using test::Mutex;
    using test::Nestable;
    using test::Number;
    using test::Spinlock;
    using test::TryNoIrqSave;

    // Test require and exclude annotations.
    {
        Foo a{};

        Guard<Mutex> guard_a{&a.lock};
        a.TestRequire();
#if TEST_WILL_NOT_COMPILE || 0
        a.TestExclude();
#endif

        guard_a.Release();
#if TEST_WILL_NOT_COMPILE || 0
        a.TestRequire();
#endif
        a.TestExclude();
    }

    // Test multiple acquire.
    {
        Foo a{};

        Guard<Mutex> guard_a{&a.lock};
#if TEST_WILL_NOT_COMPILE || 0
        Guard<Mutex> guard_b{&a.lock};
#endif
    }

    // Test sequential acquire/release.
    {
        Foo a{};

        Guard<Mutex> guard_a{&a.lock};
        guard_a.Release();
        Guard<Mutex> guard_b{&a.lock};
    }

    END_TEST;
}

UNITTEST_START_TESTCASE(lock_dep_tests)
UNITTEST("lock_dep_dynamic_analysis_tests", lock_dep_dynamic_analysis_tests)
UNITTEST("lock_dep_static_analysis_tests", lock_dep_static_analysis_tests)
UNITTEST_END_TESTCASE(lock_dep_tests, "lock_dep_tests", "lock_dep_tests");

#endif
