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

#define DEFINE_REGISTER_ACCESSOR(REG)                           \
    static inline void set_##REG(uint16_t value) {              \
        __asm__ volatile("mov %0, %%" #REG : : "r"(value));     \
    }                                                           \
    static inline uint16_t get_##REG(void) {                    \
        uint16_t value;                                         \
        __asm__ volatile("mov %%" #REG ", %0" : "=r"(value));   \
        return value;                                           \
    }

DEFINE_REGISTER_ACCESSOR(ds)
DEFINE_REGISTER_ACCESSOR(es)
DEFINE_REGISTER_ACCESSOR(fs)
DEFINE_REGISTER_ACCESSOR(gs)

#undef DEFINE_REGISTER_ACCESSOR

// This test demonstrates that if the segment selector registers are set to
// 1, they will eventually be reset to 0 when an interrupt occurs.  This is
// mostly a property of the x86 architecture rather than the kernel: The
// IRET instruction has the side effect of resetting these registers when
// returning from the kernel to userland (but not when returning to kernel
// code).
bool test_segment_selectors_zeroed_on_interrupt() {
    BEGIN_TEST;

    // We skip setting %fs because that breaks libc's TLS.
    set_ds(1);
    set_es(1);
    set_gs(1);

    // This could be interrupted by an interrupt that causes a context
    // switch, but on an unloaded machine it is more likely to be
    // interrupted by an interrupt where the handler returns without doing
    // a context switch.
    while (get_gs() == 1)
        __asm__ volatile("pause");

    EXPECT_EQ(get_ds(), 0);
    EXPECT_EQ(get_es(), 0);
    EXPECT_EQ(get_gs(), 0);

    END_TEST;
}

// Test that the kernel also resets the segment selector registers on a
// context switch, to avoid leaking their values and to match what happens
// on an interrupt.
bool test_segment_selectors_zeroed_on_context_switch() {
    BEGIN_TEST;

    set_ds(1);
    set_es(1);
    set_gs(1);

    // Sleeping should cause a context switch away from this thread (to the
    // kernel's idle thread) and another context switch back.
    //
    // It is possible that this thread is interrupted by an interrupt, but
    // not very likely, because this thread does not execute very long.
    EXPECT_EQ(mx_nanosleep(mx_deadline_after(MX_MSEC(1))), MX_OK);

    EXPECT_EQ(get_ds(), 0);
    EXPECT_EQ(get_es(), 0);
    EXPECT_EQ(get_gs(), 0);

    END_TEST;
}

BEGIN_TEST_CASE(register_state_tests)
RUN_TEST(test_context_switch_of_gs_base)
RUN_TEST(test_segment_selectors_zeroed_on_interrupt)
RUN_TEST(test_segment_selectors_zeroed_on_context_switch)
END_TEST_CASE(register_state_tests)

#endif

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
