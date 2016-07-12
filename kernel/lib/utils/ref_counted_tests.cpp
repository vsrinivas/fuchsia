// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
#include <kernel/thread.h>
#include <unittest.h>
#include <utils/ref_counted.h>
#include <utils/ref_ptr.h>

class DestructionTracker : public utils::RefCounted<DestructionTracker> {
public:
    explicit DestructionTracker(bool* destroyed)
        : destroyed_(destroyed) {}
    ~DestructionTracker() { *destroyed_ = true; }

private:
    bool* destroyed_;
};

static int inc_and_dec(void* arg) {
    DestructionTracker* tracker = reinterpret_cast<DestructionTracker*>(arg);
    for (size_t i = 0u; i < 500; ++i) {
        utils::RefPtr<DestructionTracker> ptr(tracker);
    }
    return 0;
}

static bool ref_counted_test(void* context) {
    BEGIN_TEST;

    bool destroyed = false;
    {
        utils::RefPtr<DestructionTracker> ptr = utils::AdoptRef(new DestructionTracker(&destroyed));
        EXPECT_FALSE(destroyed, "should not be destroyed");
        void* arg = reinterpret_cast<void*>(ptr.get());

        thread_t* threads[5];
        threads[0] = thread_create("inc_and_dec thread 0", &inc_and_dec, arg, DEFAULT_PRIORITY,
                                   DEFAULT_STACK_SIZE);
        threads[1] = thread_create("inc_and_dec thread 1", &inc_and_dec, arg, DEFAULT_PRIORITY,
                                   DEFAULT_STACK_SIZE);
        threads[2] = thread_create("inc_and_dec thread 2", &inc_and_dec, arg, DEFAULT_PRIORITY,
                                   DEFAULT_STACK_SIZE);
        threads[3] = thread_create("inc_and_dec thread 3", &inc_and_dec, arg, DEFAULT_PRIORITY,
                                   DEFAULT_STACK_SIZE);
        threads[4] = thread_create("inc_and_dec thread 4", &inc_and_dec, arg, DEFAULT_PRIORITY,
                                   DEFAULT_STACK_SIZE);
        for (size_t i = 0u; i < 5u; ++i)
            thread_resume(threads[i]);

        inc_and_dec(arg);

        for (size_t i = 0u; i < 5u; ++i)
            thread_join(threads[i], NULL, INFINITE_TIME);

        EXPECT_FALSE(destroyed, "should not be destroyed after inc/dec pairs");
    }
    EXPECT_TRUE(destroyed, "should be when RefPtr falls out of scope");
    END_TEST;
}

UNITTEST_START_TESTCASE(ref_counted_tests)
UNITTEST("Ref Counted", ref_counted_test)
UNITTEST_END_TESTCASE(ref_counted_tests, "refctests", "Ref Counted Tests", NULL, NULL);
