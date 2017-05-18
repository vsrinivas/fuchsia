// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <link.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/crashlogger.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/object.h>
#include <magenta/syscalls/port.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#include "utils.h"

typedef bool (wait_inferior_exception_handler_t)(mx_handle_t inferior,
                                                 const mx_exception_packet_t* packet);

// Sleep interval in the watchdog thread. Make this short so we don't need to
// wait too long when tearing down in the success case.  This is especially
// helpful when running "while /boot/test/debugger-test; do true; done".
#define WATCHDOG_DURATION_TICK ((int64_t)MX_MSEC(30))  // 0.03 seconds

// Number of sleep intervals until the watchdog fires.
#define WATCHDOG_DURATION_TICKS 100  // 3 seconds

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
// Used for testing the s/w breakpoint insn.
static const char test_swbreak_child_name[] = "swbreak";

// Setting to true when done turns off the watchdog timer.  This
// must be an atomic so that the compiler does not assume anything
// about when it can be touched.  Otherwise, since the compiler
// knows that vDSO calls don't make direct callbacks, it assumes
// that nothing can happen inside the watchdog loop that would touch
// this variable.  In fact, it will be touched in parallel by
// another thread.
static volatile atomic_bool done_tests;

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
    test_data_addr = get_uint64_register(thread, offsetof(mx_arm64_general_regs_t, r[9]));
#endif

    size_t size = read_inferior_memory(inferior, test_data_addr, test_data, sizeof(test_data));
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
    uint64_t sp = get_uint64_register(thread, offsetof(mx_arm64_general_regs_t, sp));
    set_uint64_register(thread, offsetof(mx_arm64_general_regs_t, r[8]), sp);
#endif
}

static bool test_segv_pc(mx_handle_t thread)
{
#ifdef __x86_64__
    uint64_t pc = get_uint64_register(
        thread, offsetof(mx_x86_64_general_regs_t, rip));
    uint64_t r10 = get_uint64_register(
        thread, offsetof(mx_x86_64_general_regs_t, r10));
    ASSERT_EQ(pc, r10, "fault PC does not match r10");
#endif

#ifdef __aarch64__
    uint64_t pc = get_uint64_register(
        thread, offsetof(mx_arm64_general_regs_t, pc));
    uint64_t x10 = get_uint64_register(
        thread, offsetof(mx_arm64_general_regs_t, r[10]));
    ASSERT_EQ(pc, x10, "fault PC does not match x10");
#endif

    return true;
}

// A simpler exception handler.
// Synthetic exceptions are handled internally. Architectural exceptions
// are passed on to |handler|.
// Returns false if a test fails.
// Otherwise waits for the inferior to exit and returns true.

static bool wait_inferior_thread_worker(mx_handle_t inferior,
                                        mx_handle_t eport,
                                        wait_inferior_exception_handler_t* handler)
{
    BEGIN_HELPER;

    while (true) {
        unittest_printf("wait-inf: waiting on inferior\n");

        mx_exception_packet_t packet;
        if (!read_exception(eport, &packet))
            return false;
        mx_koid_t tid = packet.report.context.tid;

        if (packet.report.header.type == MX_EXCP_THREAD_STARTING) {
            unittest_printf("wait-inf: inferior started\n");
            if (!resume_inferior(inferior, tid))
                return false;
            continue;
        } else if (packet.report.header.type == MX_EXCP_THREAD_EXITING) {
            mx_handle_t thread;
            mx_status_t status = mx_object_get_child(inferior, tid, MX_RIGHT_SAME_RIGHTS, &thread);
            // If the process has exited then the kernel may have reaped the
            // thread already. Check.
            if (status != ERR_NOT_FOUND) {
                mx_info_thread_t info = tu_thread_get_info(thread);
                // The thread could still transition to DEAD here (if the
                // process exits), so check for either DYING or DEAD.
                EXPECT_TRUE(info.state == MX_THREAD_STATE_DYING ||
                            info.state == MX_THREAD_STATE_DEAD, "");
                // If the state is DYING it would be nice to check that the value of
                // |info.wait_exception_port_type| is DEBUGGER. Alas if the process has
                // exited then the thread will get THREAD_SIGNAL_KILL which will cause
                // UserThread::ExceptionHandlerExchange to exit before we've told the
                // thread to "resume" from MX_EXCP_THREAD_EXITING. The thread is still
                // in the DYING state but it is no longer in an exception. Thus
                // |info.wait_exception_port_type| can either be DEBUGGER or NONE.
                EXPECT_TRUE(info.wait_exception_port_type == MX_EXCEPTION_PORT_TYPE_NONE ||
                            info.wait_exception_port_type == MX_EXCEPTION_PORT_TYPE_DEBUGGER, "");
                tu_handle_close(thread);
            } else {
                EXPECT_TRUE(tu_process_has_exited(inferior), "");
            }
            unittest_printf("wait-inf: thread %" PRId64 " exited\n", tid);
            // A thread is gone, but we only care about the process.
            if (!resume_inferior(inferior, tid))
                return false;
            continue;
        } else if (packet.report.header.type == MX_EXCP_GONE) {
            if (tid == 0) {
                // process is gone
                unittest_printf("wait-inf: inferior gone\n");
                break;
            }
            // A thread is gone, but we only care about the process.
            continue;
        }

        if (!handler(inferior, &packet))
            return false;
    }

    END_HELPER;
}

typedef struct {
    mx_handle_t inferior;
    mx_handle_t eport;
    wait_inferior_exception_handler_t* handler;
} wait_inf_args_t;

static int wait_inferior_thread_func(void* arg)
{
    wait_inf_args_t* args = arg;
    mx_handle_t inferior = args->inferior;
    mx_handle_t eport = args->eport;
    wait_inferior_exception_handler_t* handler = args->handler;
    free(args);

    bool pass = wait_inferior_thread_worker(inferior, eport, handler);
    return pass ? 0 : -1;
}

static int watchdog_thread_func(void* arg)
{
    for (int i = 0; i < WATCHDOG_DURATION_TICKS; ++i) {
        mx_nanosleep(mx_deadline_after(WATCHDOG_DURATION_TICK));
        if (atomic_load(&done_tests))
            return 0;
    }
    unittest_printf_critical("\n\n*** WATCHDOG TIMER FIRED ***\n");
    // This kills the entire process, not just this thread.
    // TODO(dbort): Figure out why the shell sometimes reports a zero
    // exit status when we expect to see '5'.
    exit(5);
}

static thrd_t start_wait_inf_thread(mx_handle_t inferior,
                                    mx_handle_t* out_eport,
                                    wait_inferior_exception_handler_t* handler)
{
    mx_handle_t eport = attach_inferior(inferior);
    wait_inf_args_t* args = calloc(1, sizeof(*args));

    // Both handles are loaned to the thread. The caller of this function
    // owns and must close them.
    args->inferior = inferior;
    args->eport = eport;
    args->handler = handler;

    thrd_t wait_inferior_thread;
    tu_thread_create_c11(&wait_inferior_thread, wait_inferior_thread_func,
                         (void*)args, "wait-inf thread");
    *out_eport = eport;
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

static bool expect_debugger_attached_eq(
        mx_handle_t inferior, bool expected, const char* msg) {
    mx_info_process_t info;
    // MX_ASSERT returns false if the check fails.
    ASSERT_EQ(mx_object_get_info(
            inferior, MX_INFO_PROCESS, &info, sizeof(info), NULL, NULL), NO_ERROR, "");
    ASSERT_EQ(info.debugger_attached, expected, msg);
    return true;
}

// This returns a bool as it calls ASSERT_*.
// N.B. This runs on the wait-inferior thread.

static bool debugger_test_exception_handler(mx_handle_t inferior,
                                            const mx_exception_packet_t* packet)
{
    ASSERT_EQ(packet->report.header.type, (unsigned) MX_EXCP_FATAL_PAGE_FAULT,
              "wait-inf: unexpected exception type");

    unittest_printf("wait-inf: got page fault exception\n");

    mx_koid_t tid = packet->report.context.tid;
    mx_handle_t thread = tu_get_thread(inferior, tid);

    dump_inferior_regs(thread);

    // Verify that the fault is at the PC we expected.
    if (!test_segv_pc(thread))
        return false;

    // Do some tests that require a suspended inferior.
    test_memory_ops(inferior, thread);

    fix_inferior_segv(thread);
    // Useful for debugging, otherwise a bit too verbose.
    //dump_inferior_regs(thread);

    // Increment this before resuming the inferior in case the inferior
    // sends MSG_RECOVERED_FROM_CRASH and the testcase processes the message
    // before we can increment it.
    atomic_fetch_add(&segv_count, 1);

    mx_status_t status = mx_task_resume(thread, MX_RESUME_EXCEPTION);
    tu_handle_close(thread);
    ASSERT_EQ(status, NO_ERROR, "");

    return true;
}

static bool debugger_test(void)
{
    BEGIN_TEST;

    launchpad_t* lp;
    mx_handle_t inferior, channel;
    if (!setup_inferior(test_inferior_child_name, &lp, &inferior, &channel))
        return false;

    expect_debugger_attached_eq(inferior, false, "debugger should not appear attached");
    mx_handle_t eport = MX_HANDLE_INVALID;
    thrd_t wait_inf_thread =
        start_wait_inf_thread(inferior, &eport,
                              debugger_test_exception_handler);
    EXPECT_GT(eport, 0, "");
    expect_debugger_attached_eq(inferior, true, "debugger should appear attached");

    if (!start_inferior(lp))
        return false;
    if (!verify_inferior_running(channel))
        return false;

    atomic_store(&segv_count, 0);
    enum message msg;
    send_msg(channel, MSG_CRASH_AND_RECOVER_TEST);
    if (!recv_msg(channel, &msg))
        return false;
    EXPECT_EQ(msg, MSG_RECOVERED_FROM_CRASH, "unexpected response from crash");
    EXPECT_EQ(atomic_load(&segv_count), NUM_SEGV_TRIES, "segv tests terminated prematurely");

    if (!shutdown_inferior(channel, inferior))
        return false;

    // Stop the waiter thread before closing the eport that it's waiting on.
    join_wait_inf_thread(wait_inf_thread);

    expect_debugger_attached_eq(inferior, true, "debugger should still appear attached");
    tu_handle_close(eport);
    expect_debugger_attached_eq(inferior, false, "debugger should no longer appear attached");

    tu_handle_close(channel);
    tu_handle_close(inferior);

    END_TEST;
}

static bool debugger_thread_list_test(void)
{
    BEGIN_TEST;

    launchpad_t* lp;
    mx_handle_t inferior, channel;
    if (!setup_inferior(test_inferior_child_name, &lp, &inferior, &channel))
        return false;

    mx_handle_t eport = MX_HANDLE_INVALID;
    thrd_t wait_inf_thread =
        start_wait_inf_thread(inferior, &eport,
                              debugger_test_exception_handler);
    EXPECT_GT(eport, 0, "");

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
    size_t num_threads;
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
        mx_handle_t thread = tu_get_thread(inferior, koid);
        mx_info_handle_basic_t info;
        status = mx_object_get_info(thread, MX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
        EXPECT_EQ(status, NO_ERROR, "mx_object_get_info failed");
        EXPECT_EQ(info.type, (uint32_t) MX_OBJ_TYPE_THREAD, "not a thread");
    }

    if (!shutdown_inferior(channel, inferior))
        return false;

    // Stop the waiter thread before closing the eport that it's waiting on.
    join_wait_inf_thread(wait_inf_thread);

    tu_handle_close(eport);
    tu_handle_close(channel);
    tu_handle_close(inferior);

    END_TEST;
}

static bool property_process_debug_addr_test(void)
{
    BEGIN_TEST;

    mx_handle_t self = mx_process_self();

    // We shouldn't be able to set it.
    uintptr_t debug_addr = 42;
    mx_status_t status = mx_object_set_property(self, MX_PROP_PROCESS_DEBUG_ADDR,
                                                &debug_addr, sizeof(debug_addr));
    ASSERT_EQ(status, ERR_ACCESS_DENIED, "");

    // Some minimal verification that the value is correct.

    status = mx_object_get_property(self, MX_PROP_PROCESS_DEBUG_ADDR,
                                    &debug_addr, sizeof(debug_addr));
    ASSERT_EQ(status, NO_ERROR, "");

    // These are all dsos we link with. See rules.mk.
    const char* launchpad_so = "liblaunchpad.so";
    bool found_launchpad = false;
    const char* libc_so = "libc.so";
    bool found_libc = false;
    const char* test_utils_so = "libtest-utils.so";
    bool found_test_utils = false;
    const char* unittest_so = "libunittest.so";
    bool found_unittest = false;

    const struct r_debug* r_debug = (struct r_debug*) debug_addr;
    const struct link_map* lmap = r_debug->r_map;

    EXPECT_EQ((int) r_debug->r_state, RT_CONSISTENT, "");

    while (lmap != NULL) {
        if (strcmp(lmap->l_name, launchpad_so) == 0)
            found_launchpad = true;
        else if (strcmp(lmap->l_name, libc_so) == 0)
            found_libc = true;
        else if (strcmp(lmap->l_name, test_utils_so) == 0)
            found_test_utils = true;
        else if (strcmp(lmap->l_name, unittest_so) == 0)
            found_unittest = true;
        lmap = lmap->l_next;
    }

    EXPECT_TRUE(found_launchpad, "");
    EXPECT_TRUE(found_libc, "");
    EXPECT_TRUE(found_test_utils, "");
    EXPECT_TRUE(found_unittest, "");

    END_TEST;
}

static int write_text_segment_helper(void) __ALIGNED(8);
static int write_text_segment_helper(void)
{
    /* This function needs to be at least two bytes in size as we set a
       breakpoint, figuratively speaking, on write_text_segment_helper + 1
       to ensure the address is not page aligned. Returning some random value
       will ensure that. */
    return 42;
}

static bool write_text_segment(void)
{
    BEGIN_TEST;

    mx_handle_t self = mx_process_self();

    // Exercise MG-739
    // Pretend we're writing a s/w breakpoint to the start of this function.

    // write_text_segment_helper is suitably aligned, add 1 to ensure the
    // byte we write is not page aligned.
    uintptr_t addr = (uintptr_t) write_text_segment_helper + 1;
    uint8_t previous_byte;
    size_t size = read_inferior_memory(self, addr, &previous_byte, sizeof(previous_byte));
    EXPECT_EQ(size, sizeof(previous_byte), "");

    uint8_t byte_to_write = 0;
    size = write_inferior_memory(self, addr, &byte_to_write, sizeof(byte_to_write));
    EXPECT_EQ(size, sizeof(byte_to_write), "");

    size = write_inferior_memory(self, addr, &previous_byte, sizeof(previous_byte));
    EXPECT_EQ(size, sizeof(previous_byte), "");

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
        mx_nanosleep(mx_deadline_after(MX_SEC(1)));
    return 0;
}

// This returns "bool" because it uses ASSERT_*.

static bool msg_loop(mx_handle_t channel)
{
    BEGIN_HELPER;  // Don't stomp on the main thread's current_test_info.

    bool my_done_tests = false;

    while (!atomic_load(&done_tests) && !my_done_tests)
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
        case MSG_CRASH_AND_RECOVER_TEST:
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
            // Each will require an MX_EXCP_STARTING exchange with the "debugger".
            while (atomic_load(&extra_thread_count) < NUM_EXTRA_THREADS)
                mx_nanosleep(mx_deadline_after(MX_USEC(1)));
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
    mx_handle_t channel = mx_get_startup_handle(PA_USER0);
    unittest_printf("test_inferior: got handle %d\n", channel);

    if (!msg_loop(channel))
        exit(20);

    atomic_store(&done_tests, true);
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

// Invoke the s/w breakpoint insn using the crashlogger mechanism
// to request a backtrace but not terminate the process.

static int __NO_INLINE test_swbreak(void)
{
    unittest_printf("Invoking s/w breakpoint instruction\n");
    crashlogger_request_backtrace();
    unittest_printf("Resumed after s/w breakpoint instruction\n");
    return 0;
}

BEGIN_TEST_CASE(debugger_tests)
RUN_TEST(debugger_test)
RUN_TEST(debugger_thread_list_test)
RUN_TEST(property_process_debug_addr_test)
RUN_TEST(write_text_segment)
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
    if (argc >= 2 && strcmp(argv[1], test_swbreak_child_name) == 0) {
        check_verbosity(argc, argv);
        return test_swbreak();
    }

    thrd_t watchdog_thread;
    tu_thread_create_c11(&watchdog_thread, watchdog_thread_func, NULL, "watchdog-thread");

    bool success = unittest_run_all_tests(argc, argv);

    atomic_store(&done_tests, true);
    thrd_join(watchdog_thread, NULL);
    return success ? 0 : -1;
}
