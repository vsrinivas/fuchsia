// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <pthread.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#if defined(__x86_64__)

#include <x86intrin.h>

static pthread_barrier_t g_barrier;

// Returns whether the CPU supports the {rd,wr}{fs,gs}base instructions.
static bool x86_feature_fsgsbase() {
    uint32_t eax, ebx, ecx, edx;
    __asm__("cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(7), "c"(0));
    return ebx & 1;
}

__attribute__((target("fsgsbase")))
static void* gs_base_test_thread(void* thread_arg) {
    uintptr_t gs_base = (uintptr_t)thread_arg;
    uintptr_t fs_base = 0;
    if (x86_feature_fsgsbase()) {
        _writegsbase_u64(gs_base);
        // We don't want to modify fs_base because it is used by libc etc.,
        // but we might as well check that it is also preserved.
        fs_base = _readfsbase_u64();
    }

    // Wait until all the test threads reach this point.
    int rv = pthread_barrier_wait(&g_barrier);
    assert(rv == 0 || rv == PTHREAD_BARRIER_SERIAL_THREAD);

    if (x86_feature_fsgsbase()) {
        assert(_readgsbase_u64() == gs_base);
        assert(_readfsbase_u64() == fs_base);
    }

    return nullptr;
}

// This tests whether the gs_base register on x86 is preserved across
// context switches.
//
// We do this by launching multiple threads that set gs_base to different
// values.  After all the threads have set gs_base, the threads wake up and
// check that gs_base was preserved.
bool test_context_switch_of_gs_base() {
    BEGIN_TEST;

    // We run the rest of the test even if the fsgsbase instructions aren't
    // available, so that at least the test's threading logic gets
    // exercised.
    printf("fsgsbase available = %d\n", x86_feature_fsgsbase());

    // We launch more threads than there are CPUs.  This ensures that there
    // should be at least one CPU that has >1 of our threads scheduled on
    // it, so saving and restoring gs_base between those threads should get
    // exercised.
    uint32_t thread_count = mx_system_get_num_cpus() * 2;
    ASSERT_GT(thread_count, 0);

    pthread_t tids[thread_count];
    ASSERT_EQ(pthread_barrier_init(&g_barrier, nullptr, thread_count), 0);
    for (uint32_t i = 0; i < thread_count; ++i) {
        // Give each thread a different test value for gs_base.
        void* gs_base = (void*)(uintptr_t)(i * 0x10004);
        ASSERT_EQ(pthread_create(&tids[i], nullptr, gs_base_test_thread,
                                 gs_base), 0);
    }
    for (uint32_t i = 0; i < thread_count; ++i) {
        ASSERT_EQ(pthread_join(tids[i], nullptr), 0);
    }
    ASSERT_EQ(pthread_barrier_destroy(&g_barrier), 0);

    END_TEST;
}

BEGIN_TEST_CASE(register_state_tests)
RUN_TEST(test_context_switch_of_gs_base)
END_TEST_CASE(register_state_tests)

#endif

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
