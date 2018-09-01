// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>

#include <zircon/syscalls.h>
#include <lib/zx/event.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <unittest/unittest.h>

static uint8_t DestructionTrackerStorage[32];

template <bool EnableAdoptionValidator>
class DestructionTracker : public fbl::RefCounted<DestructionTracker<EnableAdoptionValidator>,
                                                  EnableAdoptionValidator> {
public:
    explicit DestructionTracker(bool* destroyed)
        : destroyed_(destroyed) {}
    ~DestructionTracker() { *destroyed_ = true; }

    // During our death tests, we will be doing things which would normally be
    // Very Bad for actually heap allocated objects.  These tests only ever need
    // a single DestructionTracker object to be allocated at a time.  Overload
    // new/delete so that we are using statically allocated storage and avoid
    // doing bad things to our heap.
    void* operator new(size_t size) {
        ZX_ASSERT(size <= sizeof(DestructionTrackerStorage));
        return DestructionTrackerStorage;
    }

    void* operator new(size_t size, fbl::AllocChecker* ac) {
        ZX_ASSERT(ac != nullptr);
        ZX_ASSERT(size <= sizeof(DestructionTrackerStorage));
        ac->arm(size, true);
        return DestructionTrackerStorage;
    }

    void operator delete(void* ptr) {
        ZX_ASSERT(ptr == DestructionTrackerStorage);
    }

private:
    bool* destroyed_;
};

static_assert(sizeof(DestructionTracker<true>) == sizeof(DestructionTracker<false>),
              "DestructionTracker debug vs. release size mismatch!");
static_assert(sizeof(DestructionTracker<true>) <= sizeof(DestructionTrackerStorage),
              "Not enough static storage for DestructionTracker<true|false>!");

template <bool EnableAdoptionValidator>
static void* inc_and_dec(void* arg) {
    auto tracker = reinterpret_cast<DestructionTracker<EnableAdoptionValidator>*>(arg);
    for (size_t i = 0u; i < 500; ++i) {
        fbl::RefPtr<DestructionTracker<EnableAdoptionValidator>> ptr(tracker);
    }
    return nullptr;
}

template <bool EnableAdoptionValidator>
static bool ref_counted_test() {
    BEGIN_TEST;

    bool destroyed = false;
    {
        fbl::AllocChecker ac;
        fbl::RefPtr<DestructionTracker<EnableAdoptionValidator>> ptr =
            fbl::AdoptRef(new (&ac) DestructionTracker<EnableAdoptionValidator>(&destroyed));
        EXPECT_TRUE(ac.check());

        EXPECT_FALSE(destroyed, "should not be destroyed");
        void* arg = reinterpret_cast<void*>(ptr.get());

        pthread_t threads[5];
        for (size_t i = 0u; i < fbl::count_of(threads); ++i) {
            int res = pthread_create(&threads[i], NULL, &inc_and_dec<EnableAdoptionValidator>, arg);
            ASSERT_LE(0, res, "Failed to create inc_and_dec thread!");
        }

        inc_and_dec<EnableAdoptionValidator>(arg);

        for (size_t i = 0u; i < fbl::count_of(threads); ++i)
            pthread_join(threads[i], NULL);

        EXPECT_FALSE(destroyed, "should not be destroyed after inc/dec pairs");
    }
    EXPECT_TRUE(destroyed, "should be when RefPtr falls out of scope");
    END_TEST;
}

template <bool EnableAdoptionValidator>
static bool make_ref_counted_test() {
    BEGIN_TEST;

    bool destroyed = false;
    {
        auto ptr = fbl::MakeRefCounted<DestructionTracker<EnableAdoptionValidator>>(&destroyed);
        EXPECT_FALSE(destroyed, "should not be destroyed");
    }
    EXPECT_TRUE(destroyed, "should be when RefPtr falls out of scope");

    destroyed = false;
    {
        fbl::AllocChecker ac;
        auto ptr2 = fbl::MakeRefCountedChecked<DestructionTracker<EnableAdoptionValidator>>(
                &ac, &destroyed);
        EXPECT_TRUE(ac.check());
    }
    EXPECT_TRUE(destroyed, "should be when RefPtr falls out of scope");

    END_TEST;
}

static bool wrap_dead_pointer_asserts() {
    BEGIN_TEST;

    bool destroyed = false;
    DestructionTracker<true>* raw = nullptr;
    {
        // Create and adopt a ref-counted object, and let it go out of scope.
        fbl::AllocChecker ac;
        fbl::RefPtr<DestructionTracker<true>> ptr =
            fbl::AdoptRef(new (&ac) DestructionTracker<true>(&destroyed));
        EXPECT_TRUE(ac.check());
        raw = ptr.get();
        EXPECT_FALSE(destroyed);
    }
    EXPECT_TRUE(destroyed);

    // Wrapping the now-destroyed object should trigger an assertion.
    ASSERT_DEATH(
            [](void* void_raw) {
                auto raw = reinterpret_cast<DestructionTracker<true>*>(void_raw);
                __UNUSED fbl::RefPtr<DestructionTracker<true>> zombie = fbl::WrapRefPtr(raw);
            },
            raw, "Assert should have fired after wraping dead object\n");

    END_TEST;
}

static bool extra_release_asserts() {
    BEGIN_TEST;

    // Create and adopt a ref-counted object.
    bool destroyed = false;
    fbl::AllocChecker ac;
    DestructionTracker<true>* raw = new (&ac) DestructionTracker<true>(&destroyed);
    ASSERT_TRUE(ac.check());
    raw->Adopt();

    // Manually release once, which should tell us to delete the object.
    EXPECT_TRUE(raw->Release());
    // (But it's not deleted since we didn't listen to the return value
    // of Release())
    EXPECT_FALSE(destroyed);

    ASSERT_DEATH(
            [](void* void_raw) {
                auto raw = reinterpret_cast<DestructionTracker<true>*>(void_raw);
                // Manually releasing again should trigger the assertion.
                __UNUSED bool unused = raw->Release();
            },
            raw, "Assert should have fired after releasing object with ref count of zero\n");

    // Do not attempt to actually delete the object.  It was never actually heap
    // allocated, so we are not leaking anything, and the system is in a bad
    // state now.  Attempting to delete the object can trigger other ASSERTs
    // which will crash the test.

    END_TEST;
}

static bool wrap_after_last_release_asserts() {
    BEGIN_TEST;

    // Create and adopt a ref-counted object.
    bool destroyed = false;
    fbl::AllocChecker ac;
    DestructionTracker<true>* raw = new (&ac) DestructionTracker<true>(&destroyed);
    ASSERT_TRUE(ac.check());
    raw->Adopt();

    // Manually release once, which should tell us to delete the object.
    EXPECT_TRUE(raw->Release());
    // (But it's not deleted since we didn't listen to the return value
    // of Release())
    EXPECT_FALSE(destroyed);

    ASSERT_DEATH(
            [](void* void_raw) {
                auto raw = reinterpret_cast<DestructionTracker<true>*>(void_raw);
                // Adding another ref (by wrapping) should trigger the assertion.
                __UNUSED bool unused = raw->Release();
            },
            raw, "Assert should have fired after wraping object with ref count of zero\n");

    // Do not attempt to actually delete the object.  See previous comments.

    END_TEST;
}

static bool unadopted_add_ref_asserts() {
    BEGIN_TEST;

    // Create an un-adopted ref-counted object.
    bool destroyed = false;
    fbl::AllocChecker ac;
    DestructionTracker<true>* raw = new (&ac) DestructionTracker<true>(&destroyed);
    ASSERT_TRUE(ac.check());

    ASSERT_DEATH(
            [](void* void_raw) {
                auto raw = reinterpret_cast<DestructionTracker<true>*>(void_raw);
                // Adding a ref (by wrapping) without adopting first should trigger an
                // assertion.
                fbl::RefPtr<DestructionTracker<true>> unadopted = fbl::WrapRefPtr(raw);
            },
            raw, "Assert should have fired after wraping non-adopted object\n");

    // Do not attempt to actually delete the object.  See previous comments.

    END_TEST;
}

static bool unadopted_release_asserts() {
    BEGIN_TEST;

    // Create an un-adopted ref-counted object.
    bool destroyed = false;
    fbl::AllocChecker ac;
    DestructionTracker<true>* raw = new (&ac) DestructionTracker<true>(&destroyed);
    ASSERT_TRUE(ac.check());

    ASSERT_DEATH(
            [](void* void_raw) {
                auto raw = reinterpret_cast<DestructionTracker<true>*>(void_raw);
                // Releasing without adopting first should trigger an assertion.
                __UNUSED bool unused = raw->Release();
            },
            raw, "Assert should have fired after releasing non-adopted object\n");

    // Do not attempt to actually delete the object.  See previous comments.

    END_TEST;
}

namespace {
template <bool EnableAdoptionValidator>
class RawUpgradeTester : public fbl::RefCounted<RawUpgradeTester<EnableAdoptionValidator>,
                                                EnableAdoptionValidator> {
public:
    RawUpgradeTester(fbl::Mutex* mutex, fbl::atomic<bool>* destroying, zx::event* event)
        : mutex_(mutex), destroying_(destroying), destroying_event_(event) {}

    ~RawUpgradeTester() {
        atomic_store(destroying_, true);
        if (destroying_event_)
            destroying_event_->signal(0u, ZX_EVENT_SIGNALED);
        fbl::AutoLock al(mutex_);
    }

private:
    fbl::Mutex* mutex_;
    fbl::atomic<bool>* destroying_;
    zx::event* destroying_event_;
};

template <bool EnableAdoptionValidator>
void* adopt_and_reset(void* arg) {
    fbl::RefPtr<RawUpgradeTester<EnableAdoptionValidator>> rc_client =
        fbl::AdoptRef(reinterpret_cast<RawUpgradeTester<EnableAdoptionValidator>*>(arg));
    // The reset() which will call the dtor, which we expect to
    // block because upgrade_fail_test() is holding the mutex.
    rc_client.reset();
    return NULL;
}

} // namespace

template <bool EnableAdoptionValidator>
static bool upgrade_fail_test() {
    BEGIN_TEST;

    fbl::Mutex mutex;
    fbl::AllocChecker ac;
    fbl::atomic<bool> destroying{false};
    zx::event destroying_event;

    zx_status_t status = zx::event::create(0u, &destroying_event);
    ASSERT_EQ(status, ZX_OK);

    auto raw = new (&ac) RawUpgradeTester<EnableAdoptionValidator>(&mutex,
                                                                   &destroying,
                                                                   &destroying_event);
    EXPECT_TRUE(ac.check());

    pthread_t thread;
    {
        fbl::AutoLock al(&mutex);
        int res = pthread_create(&thread, NULL, &adopt_and_reset<EnableAdoptionValidator>, raw);
        ASSERT_LE(0, res);
        // Wait until the thread is in the destructor.
        status = destroying_event.wait_one(ZX_EVENT_SIGNALED, zx::time::infinite(), nullptr);
        EXPECT_EQ(status, ZX_OK);
        EXPECT_TRUE(atomic_load(&destroying));
        // The RawUpgradeTester must be blocked in the destructor, the upgrade will fail.
        auto upgrade1 = fbl::internal::MakeRefPtrUpgradeFromRaw(raw, mutex);
        EXPECT_FALSE(upgrade1);
        // Verify that the previous upgrade attempt did not change the refcount.
        auto upgrade2 = fbl::internal::MakeRefPtrUpgradeFromRaw(raw, mutex);
        EXPECT_FALSE(upgrade2);
    }

    pthread_join(thread, NULL);
    END_TEST;
}

template <bool EnableAdoptionValidator>
static bool upgrade_success_test() {
    BEGIN_TEST;

    fbl::Mutex mutex;
    fbl::AllocChecker ac;
    fbl::atomic<bool> destroying{false};

    auto ref = fbl::AdoptRef(new (&ac) RawUpgradeTester<EnableAdoptionValidator>(&mutex,
                                                                                 &destroying,
                                                                                 nullptr));
    EXPECT_TRUE(ac.check());
    auto raw = ref.get();

    {
        fbl::AutoLock al(&mutex);
        // RawUpgradeTester is not in the destructor so the upgrade should
        // succeed.
        auto upgrade = fbl::internal::MakeRefPtrUpgradeFromRaw(raw, mutex);
        EXPECT_TRUE(upgrade);
    }

    ref.reset();
    EXPECT_TRUE(atomic_load(&destroying));

    END_TEST;
}

BEGIN_TEST_CASE(ref_counted_tests)
RUN_NAMED_TEST("Ref Counted (adoption validation on)", ref_counted_test<true>)
RUN_NAMED_TEST("Ref Counted (adoption validation off)", ref_counted_test<false>)
RUN_NAMED_TEST("Make Ref Counted (adoption validation on)", make_ref_counted_test<true>)
RUN_NAMED_TEST("Make Ref Counted (adoption validation off)", make_ref_counted_test<false>)
RUN_NAMED_TEST("Wrapping dead pointer should assert", wrap_dead_pointer_asserts)
RUN_NAMED_TEST("Extra release should assert", extra_release_asserts)
RUN_NAMED_TEST("Wrapping zero-count pointer should assert",
               wrap_after_last_release_asserts)
RUN_NAMED_TEST("AddRef on unadopted object should assert",
               unadopted_add_ref_asserts)
RUN_NAMED_TEST("Release on unadopted object should assert",
               unadopted_release_asserts)
RUN_NAMED_TEST("Fail to upgrade raw pointer (adoption validation on)", upgrade_fail_test<true>)
RUN_NAMED_TEST("Fail to upgrade raw pointer (adoption validation off)", upgrade_fail_test<false>)
RUN_NAMED_TEST("Upgrade raw pointer (adoption validation on)", upgrade_success_test<true>)
RUN_NAMED_TEST("Upgrade raw pointer (adoption validation off)", upgrade_success_test<false>)
END_TEST_CASE(ref_counted_tests);
