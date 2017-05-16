// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxalloc/new.h>
#include <pthread.h>
#include <unittest/unittest.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>

class DestructionTracker : public mxtl::RefCounted<DestructionTracker> {
public:
    explicit DestructionTracker(bool* destroyed)
        : destroyed_(destroyed) {}
    ~DestructionTracker() { *destroyed_ = true; }

private:
    bool* destroyed_;
};

static void* inc_and_dec(void* arg) {
    DestructionTracker* tracker = reinterpret_cast<DestructionTracker*>(arg);
    for (size_t i = 0u; i < 500; ++i) {
        mxtl::RefPtr<DestructionTracker> ptr(tracker);
    }
    return nullptr;
}

static bool ref_counted_test() {
    BEGIN_TEST;

    bool destroyed = false;
    {
        AllocChecker ac;
        mxtl::RefPtr<DestructionTracker> ptr =
            mxtl::AdoptRef(new (&ac) DestructionTracker(&destroyed));
        EXPECT_TRUE(ac.check(), "");

        EXPECT_FALSE(destroyed, "should not be destroyed");
        void* arg = reinterpret_cast<void*>(ptr.get());

        pthread_t threads[5];
        for (size_t i = 0u; i < countof(threads); ++i) {
            int res = pthread_create(&threads[i], NULL, &inc_and_dec, arg);
            ASSERT_LE(0, res, "Failed to create inc_and_dec thread!");
        }

        inc_and_dec(arg);

        for (size_t i = 0u; i < countof(threads); ++i)
            pthread_join(threads[i], NULL);

        EXPECT_FALSE(destroyed, "should not be destroyed after inc/dec pairs");
    }
    EXPECT_TRUE(destroyed, "should be when RefPtr falls out of scope");
    END_TEST;
}

BEGIN_TEST_CASE(ref_counted_tests)
RUN_NAMED_TEST("Ref Counted", ref_counted_test)
END_TEST_CASE(ref_counted_tests);
