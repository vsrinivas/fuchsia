// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/port.h>
#include <mxio/util.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#include "utils.h"

// 0.5 seconds
#define WATCHDOG_DURATION_TICK ((int64_t) 500 * 1000 * 1000)
// 5 seconds
#define WATCHDOG_DURATION_TICKS 10

#define TEST_MEMORY_SIZE 8
#define TEST_DATA_ADJUST 0x10

// Do the segv recovery test a number of times to stress test the API.
#define NUM_SEGV_TRIES 4

#define NUM_EXTRA_THREADS 4

// Produce a backtrace of sufficient size to be interesting but not excessive.
#define TEST_SEGFAULT_DEPTH 4

static const char test_inferior_child_name[] = "inferior";
// The segfault child is not used by the test.
// It exists for debugging purposes.
static const char test_segfault_child_name[] = "segfault";

static bool done_tests = false;

static atomic_int extra_thread_count = ATOMIC_VAR_INIT(0);

static atomic_int segv_count = ATOMIC_VAR_INIT(0);

static void test_memory_ops(mx_handle_t inferior, mx_handle_t thread)
{
    uint64_t test_data_addr = 0;
    uint8_t test_data[TEST_MEMORY_SIZE];

#ifdef __x86_64__
    test_data_addr = get_uint64_register(thread, offsetof(mx_x86_64_general_regs_t, r9));
#endif
#ifdef __aarch64__
    test_data_addr = get_uint64_register(thread, offsetof(mx_aarch64_general_regs_t, r[9]));
#endif

    mx_size_t size = read_inferior_memory(inferior, test_data_addr, test_data, sizeof(test_data));
    EXPECT_EQ(size, sizeof(test_data), "read_inferior_memory: short read");

    for (unsigned i = 0; i < sizeof(test_data); ++i) {
        EXPECT_EQ(test_data[i], i, "test_memory_ops");
    }

    for (unsigned i = 0; i < sizeof(test_data); ++i)
        test_data[i] += TEST_DATA_ADJUST;

    size = write_inferior_memory(inferior, test_data_addr, test_data, sizeof(test_data));
    EXPECT_EQ(size, sizeof(test_data), "write_inferior_memory: short write");

    // Note: Verification of the write is done in the inferior.
}

static void fix_inferior_segv(mx_handle_t thread)
{
    unittest_printf("Fixing inferior segv\n");

#ifdef __x86_64__
    // The segv was because r8 == 0, change it to a usable value.
    // See test_prep_and_segv.
    uint64_t rsp = get_uint64_register(thread, offsetof(mx_x86_64_general_regs_t, rsp));
    set_uint64_register(thread, offsetof(mx_x86_64_general_regs_t, r8), rsp);
#endif

#ifdef __aarch64__
    // The segv was because r8 == 0, change it to a usable value.
    // See test_prep_and_segv.
    uint64_t sp = get_uint64_register(thread, offsetof(mx_aarch64_general_regs_t, sp));
    set_uint64_register(thread, offsetof(mx_aarch64_general_regs_t, r[8]), sp);
#endif
}

static bool test_segv_pc(mx_handle_t thread) {
#ifdef __x86_64__
    uint64_t pc = get_uint64_register(
        thread, offsetof(mx_x86_64_general_regs_t, rip));
    uint64_t r10 = get_uint64_register(
        thread, offsetof(mx_x86_64_general_regs_t, r10));
    ASSERT_EQ(pc, r10, "fault PC does not match r10");
#endif

#ifdef __aarch64__
    uint64_t pc = get_uint64_register(
        thread, offsetof(mx_aarch64_general_regs_t, pc));
    uint64_t x10 = get_uint64_register(
        thread, offsetof(mx_aarch64_general_regs_t, r[10]));
    ASSERT_EQ(pc, x10, "fault PC does not match x10");
#endif

    return true;
}

// Returns false if a test fails.
// Otherwise waits for the inferior to exit and returns true.

static bool wait_inferior_thread_worker(mx_handle_t inferior, mx_handle_t eport)
{
    while (true) {
        unittest_printf("wait-inf: waiting on inferior\n");

        mx_exception_packet_t packet;
        if (!read_exception(eport, &packet))
            return false;
        if (packet.report.header.type == MX_EXCP_START) {
            unittest_printf("wait-inf: inferior started\n");
            if (!resume_inferior(inferior, packet.report.context.tid))
                return false;
            continue;
        } else if (packet.report.header.type == MX_EXCP_GONE) {
            if (packet.report.context.tid == 0) {
                // process is gone
                unittest_printf("wait-inf: inferior gone\n");
                break;
            }
            // A thread is gone, but we only care about the process.
            continue;
        } else if (packet.report.header.type == MX_EXCP_FATAL_PAGE_FAULT) {
            unittest_printf("wait-inf: got page fault exception\n");
            atomic_fetch_add(&segv_count, 1);
        } else {
            ASSERT_EQ(false, true, "wait-inf: unexpected exception type");
        }

        mx_koid_t tid = packet.report.context.tid;
        mx_handle_t thread;
        mx_status_t status = mx_object_get_child(inferior, tid, MX_RIGHT_SAME_RIGHTS, &thread);
        ASSERT_EQ(status, 0, "mx_object_get_child failed");

        dump_inferior_regs(thread);

        // Verify that the fault is at the PC we expected.
        if (!test_segv_pc(thread))
            return false;

        // Do some tests that require a suspended inferior.
        test_memory_ops(inferior, thread);

        // Now correct the issue and resume the inferior.

        fix_inferior_segv(thread);
        // Useful for debugging, otherwise a bit too verbose.
        //dump_inferior_regs(thread);

        status = mx_task_resume(thread, MX_RESUME_EXCEPTION);
        tu_handle_close(thread);
        ASSERT_EQ(status, NO_ERROR, "mx_task_resume failed");
    }

    return true;
}

static int wait_inferior_thread_func(void* arg)
{
    mx_handle_t* args = arg;
    mx_handle_t inferior = args[0];
    mx_handle_t eport = args[1];

    bool pass = wait_inferior_thread_worker(inferior, eport);

    tu_handle_close(eport);

    return pass ? 0 : -1;
}

static int watchdog_thread_func(void* arg)
{
    for (int i = 0; i < WATCHDOG_DURATION_TICKS; ++i) {
        mx_nanosleep(WATCHDOG_DURATION_TICK);
        if (done_tests)
            return 0;
    }
    unittest_printf("WATCHDOG TIMER FIRED\n");
    // This should kill the entire process, not just this thread.
    exit(5);
}

static thrd_t start_wait_inf_thread(mx_handle_t inferior)
{
    mx_handle_t eport = attach_inferior(inferior);
    mx_handle_t wait_inf_args[2] = { inferior, eport };
    thrd_t wait_inferior_thread;
    // |inferior| is loaned to the thread, whereas |eport| is transfered to the thread.
    tu_thread_create_c11(&wait_inferior_thread, wait_inferior_thread_func, (void*) &wait_inf_args[0], "wait-inf thread");
    return wait_inferior_thread;
}

static void join_wait_inf_thread(thrd_t wait_inf_thread)
{
    unittest_printf("Waiting for wait-inf thread\n");
    int thread_rc;
    int ret = thrd_join(wait_inf_thread, &thread_rc);
    EXPECT_EQ(ret, thrd_success, "thrd_join failed");
    EXPECT_EQ(thread_rc, 0, "unexpected wait-inf return");
    unittest_printf("wait-inf thread done\n");
}

static bool debugger_test(void)
{
    BEGIN_TEST;

    launchpad_t* lp;
    mx_handle_t channel, inferior;
    if (!setup_inferior(test_inferior_child_name, &lp, &inferior, &channel))
        return false;

    thrd_t wait_inf_thread = start_wait_inf_thread(inferior);

    if (!start_inferior(lp))
        return false;
    if (!verify_inferior_running(channel))
        return false;

    atomic_store(&segv_count, 0);
    enum message msg;
    send_msg(channel, MSG_CRASH);
    if (!recv_msg(channel, &msg))
        return false;
    EXPECT_EQ(msg, MSG_RECOVERED_FROM_CRASH, "unexpected response from crash");
    EXPECT_EQ(atomic_load(&segv_count), NUM_SEGV_TRIES, "segv tests terminated prematurely");

    if (!shutdown_inferior(channel, inferior))
        return false;
    tu_handle_close(channel);
    tu_handle_close(inferior);

    join_wait_inf_thread(wait_inf_thread);

    END_TEST;
}

static bool debugger_thread_list_test(void)
{
    BEGIN_TEST;

    launchpad_t* lp;
    mx_handle_t channel, inferior;
    if (!setup_inferior(test_inferior_child_name, &lp, &inferior, &channel))
        return false;

    thrd_t wait_inf_thread = start_wait_inf_thread(inferior);

    if (!start_inferior(lp))
        return false;
    if (!verify_inferior_running(channel))
        return false;

    enum message msg;
    send_msg(channel, MSG_START_EXTRA_THREADS);
    if (!recv_msg(channel, &msg))
        return false;
    EXPECT_EQ(msg, MSG_EXTRA_THREADS_STARTED, "unexpected response when starting extra threads");

    uint32_t buf_size = 100 * sizeof(mx_koid_t);
    mx_size_t num_threads;
    mx_koid_t* threads = tu_malloc(buf_size);
    mx_status_t status = mx_object_get_info(inferior, MX_INFO_PROCESS_THREADS,
                                            threads, buf_size, &num_threads, NULL);
    ASSERT_EQ(status, NO_ERROR, "");

    // There should be at least 1+NUM_EXTRA_THREADS threads in the result.
    ASSERT_GE(num_threads, (unsigned)(1 + NUM_EXTRA_THREADS), "mx_object_get_info failed");

    // Verify each entry is valid.
    for (uint32_t i = 0; i < num_threads; ++i) {
        mx_koid_t koid = threads[i];
        unittest_printf("Looking up thread %llu\n", (long long) koid);
        mx_handle_t thread;
        status = mx_object_get_child(inferior, koid, MX_RIGHT_SAME_RIGHTS, &thread);
        EXPECT_EQ(status, 0, "mx_object_get_child failed");
        mx_info_handle_basic_t info;
        status = mx_object_get_info(thread, MX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
        EXPECT_EQ(status, NO_ERROR, "mx_object_get_info failed");
        EXPECT_EQ(info.type, (uint32_t) MX_OBJ_TYPE_THREAD, "not a thread");
    }

    if (!shutdown_inferior(channel, inferior))
        return false;
    tu_handle_close(channel);
    tu_handle_close(inferior);

    join_wait_inf_thread(wait_inf_thread);

    END_TEST;
}

// This function is marked as no-inline to avoid duplicate label in case the
// function call is being inlined.
__NO_INLINE static bool test_prep_and_segv(void)
{
    uint8_t test_data[TEST_MEMORY_SIZE];
    for (unsigned i = 0; i < sizeof(test_data); ++i)
        test_data[i] = i;

#ifdef __x86_64__
    void* segv_pc;
    // Note: Fuchsia is always PIC.
    __asm__("leaq .Lsegv_here(%%rip),%0" : "=r" (segv_pc));
    unittest_printf("About to segv, pc %p\n", segv_pc);

    // Set r9 to point to test_data so we can easily access it
    // from the parent process.  Likewise set r10 to segv_pc
    // so the parent process can verify it matches the fault PC.
    __asm__("\
        movq %[zero],%%r8\n\
        movq %[test_data],%%r9\n\
        movq %[pc],%%r10\n\
.Lsegv_here:\n\
        movq (%%r8),%%rax\
" : :
            [zero] "g" (0),
            [test_data] "g" (&test_data[0]),
            [pc] "g" (segv_pc) :
            "rax", "r8", "r9", "r10");
#endif

#ifdef __aarch64__
    void* segv_pc;
    // Note: Fuchsia is always PIC.
    __asm__("adrp %0, .Lsegv_here\n"
            "add %0, %0, :lo12:.Lsegv_here" : "=r" (segv_pc));
    unittest_printf("About to segv, pc %p\n", segv_pc);

    // Set r9 to point to test_data so we can easily access it
    // from the parent process.  Likewise set r10 to segv_pc
    // so the parent process can verify it matches the fault PC.
    __asm__("\
        mov x8,xzr\n\
        mov x9,%[test_data]\n\
        mov x10,%[pc]\n\
.Lsegv_here:\n\
        ldr x0,[x8]\
" : :
            [test_data] "r" (&test_data[0]),
            [pc] "r" (segv_pc) :
            "x0", "x8", "x9", "x10");
#endif

    // On resumption test_data should have had TEST_DATA_ADJUST added to each element.
    // Note: This is the inferior process, it's not running under the test harness.
    for (unsigned i = 0; i < sizeof(test_data); ++i) {
        if (test_data[i] != i + TEST_DATA_ADJUST) {
            unittest_printf("test_prep_and_segv: bad data on resumption, test_data[%u] = 0x%x\n",
                            i, test_data[i]);
            return false;
        }
    }

    unittest_printf("Inferior successfully resumed!\n");

    return true;
}

static int extra_thread_func(void* arg)
{
    atomic_fetch_add(&extra_thread_count, 1);
    unittest_printf("Extra thread started.\n");
    while (true)
        mx_nanosleep(1000 * 1000 * 1000);
    return 0;
}

// This returns "bool" because it uses ASSERT_*.

static bool msg_loop(mx_handle_t channel)
{
    BEGIN_HELPER;

    bool my_done_tests = false;

    while (!done_tests && !my_done_tests)
    {
        enum message msg;
        ASSERT_TRUE(recv_msg(channel, &msg), "Error while receiving msg");
        switch (msg)
        {
        case MSG_DONE:
            my_done_tests = true;
            break;
        case MSG_PING:
            send_msg(channel, MSG_PONG);
            break;
        case MSG_CRASH:
            for (int i = 0; i < NUM_SEGV_TRIES; ++i) {
                if (!test_prep_and_segv())
                    exit(21);
            }
            send_msg(channel, MSG_RECOVERED_FROM_CRASH);
            break;
        case MSG_START_EXTRA_THREADS:
            for (int i = 0; i < NUM_EXTRA_THREADS; ++i) {
                // For our purposes, we don't need to track the threads.
                // They'll be terminated when the process exits.
                thrd_t thread;
                tu_thread_create_c11(&thread, extra_thread_func, NULL, "extra-thread");
            }
            // Wait for all threads to be started.
            // Each will require an MX_EXCP_START exchange with the "debugger".
            while (atomic_load(&extra_thread_count) < NUM_EXTRA_THREADS)
                mx_nanosleep(1000);
            send_msg(channel, MSG_EXTRA_THREADS_STARTED);
            break;
        default:
            unittest_printf("unknown message received: %d\n", msg);
            break;
        }
    }

    END_HELPER;
}

void test_inferior(void)
{
    mx_handle_t channel = mxio_get_startup_handle(MX_HND_TYPE_USER0);
    unittest_printf("test_inferior: got handle %d\n", channel);

    if (!msg_loop(channel))
        exit(20);

    done_tests = true;
    unittest_printf("Inferior done\n");
    exit(1234);
}

// Compilers are getting too smart.
// These maintain the semantics we want even under optimization.

volatile int* crashing_ptr = (int*) 42;
volatile int crash_depth;

// This is used to cause fp != sp when the crash happens on arm64.
int leaf_stack_size = 10;

static int __NO_INLINE test_segfault_doit2(int*);

static int __NO_INLINE test_segfault_leaf(int n, int* p)
{
    volatile int x[n];
    x[0] = *p;
    *crashing_ptr = x[0];
    return 0;
}

static int __NO_INLINE test_segfault_doit1(int* p)
{
    if (crash_depth > 0)
    {
        int n = crash_depth;
        int use_stack[n];
        memset(use_stack, 0x99, n * sizeof(int));
        --crash_depth;
        return test_segfault_doit2(use_stack) + 99;
    }
    return test_segfault_leaf(leaf_stack_size, p) + 99;
}

static int __NO_INLINE test_segfault_doit2(int* p)
{
    return test_segfault_doit1(p) + *p;
}

// Produce a crash with a moderately interesting backtrace.

static int __NO_INLINE test_segfault(void)
{
    crash_depth = TEST_SEGFAULT_DEPTH;
    int i = 0;
    return test_segfault_doit1(&i);
}

BEGIN_TEST_CASE(debugger_tests)
RUN_TEST(debugger_test);
RUN_TEST(debugger_thread_list_test);
END_TEST_CASE(debugger_tests)

static void check_verbosity(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "v=", 2) == 0) {
            int verbosity = atoi(argv[i] + 2);
            unittest_set_verbosity_level(verbosity);
            break;
        }
    }
}

int main(int argc, char **argv)
{
    program_path = argv[0];

    if (argc >= 2 && strcmp(argv[1], test_inferior_child_name) == 0) {
        check_verbosity(argc, argv);
        test_inferior();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], test_segfault_child_name) == 0) {
        check_verbosity(argc, argv);
        return test_segfault();
    }

    thrd_t watchdog_thread;
    tu_thread_create_c11(&watchdog_thread, watchdog_thread_func, NULL, "watchdog-thread");

    bool success = unittest_run_all_tests(argc, argv);

    done_tests = true;
    thrd_join(watchdog_thread, NULL);
    return success ? 0 : -1;
}
