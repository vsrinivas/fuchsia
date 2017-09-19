// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <inttypes.h>
#include <link.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <zircon/crashlogger.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#include "utils.h"

typedef bool (wait_inferior_exception_handler_t)(zx_handle_t inferior,
                                                 const zx_port_packet_t* packet,
                                                 void* handler_arg);

// Sleep interval in the watchdog thread. Make this short so we don't need to
// wait too long when tearing down in the success case.  This is especially
// helpful when running "while /boot/test/debugger-test; do true; done".
#define WATCHDOG_DURATION_TICK ((int64_t)ZX_MSEC(30))  // 0.03 seconds

// Number of sleep intervals until the watchdog fires.
#define WATCHDOG_DURATION_TICKS 100  // 3 seconds

#define TEST_MEMORY_SIZE 8
#define TEST_DATA_ADJUST 0x10

// Do the segv recovery test a number of times to stress test the API.
#define NUM_SEGV_TRIES 4

#define NUM_EXTRA_THREADS 4

// Produce a backtrace of sufficient size to be interesting but not excessive.
#define TEST_SEGFAULT_DEPTH 4

// Offsets of $pc,$sp.
#ifdef __x86_64__
#define PC_REG_OFFSET offsetof(zx_x86_64_general_regs_t, rip)
#define SP_REG_OFFSET offsetof(zx_x86_64_general_regs_t, rsp)
#endif
#ifdef __aarch64__
#define PC_REG_OFFSET offsetof(zx_arm64_general_regs_t, pc)
#define SP_REG_OFFSET offsetof(zx_arm64_general_regs_t, sp)
#endif

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

static void test_memory_ops(zx_handle_t inferior, zx_handle_t thread)
{
    uint64_t test_data_addr = 0;
    uint8_t test_data[TEST_MEMORY_SIZE];

#ifdef __x86_64__
    test_data_addr = get_uint64_register(thread, offsetof(zx_x86_64_general_regs_t, r9));
#endif
#ifdef __aarch64__
    test_data_addr = get_uint64_register(thread, offsetof(zx_arm64_general_regs_t, r[9]));
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

static void fix_inferior_segv(zx_handle_t thread)
{
    unittest_printf("Fixing inferior segv\n");

    uint64_t sp = get_uint64_register(thread, SP_REG_OFFSET);

    // The segv was because r8 == 0, change it to a usable value.
    // See test_prep_and_segv.
#ifdef __x86_64__
    set_uint64_register(thread, offsetof(zx_x86_64_general_regs_t, r8), sp);
#endif
#ifdef __aarch64__
    set_uint64_register(thread, offsetof(zx_arm64_general_regs_t, r[8]), sp);
#endif
}

static bool test_segv_pc(zx_handle_t thread)
{
    uint64_t pc = get_uint64_register(thread, PC_REG_OFFSET);

#ifdef __x86_64__
    uint64_t r10 = get_uint64_register(
        thread, offsetof(zx_x86_64_general_regs_t, r10));
    ASSERT_EQ(pc, r10, "fault PC does not match r10");
#endif

#ifdef __aarch64__
    uint64_t x10 = get_uint64_register(
        thread, offsetof(zx_arm64_general_regs_t, r[10]));
    ASSERT_EQ(pc, x10, "fault PC does not match x10");
#endif

    return true;
}

// A simpler exception handler.
// All exceptions are passed on to |handler|.
// Returns false if a test fails.
// Otherwise waits for the inferior to exit and returns true.

static bool wait_inferior_thread_worker(zx_handle_t inferior,
                                        zx_handle_t eport,
                                        wait_inferior_exception_handler_t* handler,
                                        void* handler_arg)
{
    while (true) {
        zx_port_packet_t packet;
        if (!read_exception(eport, inferior, &packet))
            return false;

        // Is the inferior gone?
        if (packet.type == ZX_EXCP_GONE && packet.exception.tid == 0) {
            unittest_printf("wait-inf: inferior gone\n");
            return true;
        }

        if (!handler(inferior, &packet, handler_arg))
            return false;
    }
}

typedef struct {
    zx_handle_t inferior;
    zx_handle_t eport;
    wait_inferior_exception_handler_t* handler;
    void* handler_arg;
} wait_inf_args_t;

static int wait_inferior_thread_func(void* arg)
{
    wait_inf_args_t* args = arg;
    zx_handle_t inferior = args->inferior;
    zx_handle_t eport = args->eport;
    wait_inferior_exception_handler_t* handler = args->handler;
    void* handler_arg = args->handler_arg;
    free(args);

    bool pass = wait_inferior_thread_worker(inferior, eport, handler, handler_arg);
    return pass ? 0 : -1;
}

static int watchdog_thread_func(void* arg)
{
    for (int i = 0; i < WATCHDOG_DURATION_TICKS; ++i) {
        zx_nanosleep(zx_deadline_after(WATCHDOG_DURATION_TICK));
        if (atomic_load(&done_tests))
            return 0;
    }
    unittest_printf_critical("\n\n*** WATCHDOG TIMER FIRED ***\n");
    // This kills the entire process, not just this thread.
    // TODO(dbort): Figure out why the shell sometimes reports a zero
    // exit status when we expect to see '5'.
    exit(5);
}

static thrd_t start_wait_inf_thread(zx_handle_t inferior,
                                    zx_handle_t* out_eport,
                                    wait_inferior_exception_handler_t* handler,
                                    void* handler_arg)
{
    zx_handle_t eport = attach_inferior(inferior);
    wait_inf_args_t* args = calloc(1, sizeof(*args));

    // Both handles are loaned to the thread. The caller of this function
    // owns and must close them.
    args->inferior = inferior;
    args->eport = eport;
    args->handler = handler;
    args->handler_arg = handler_arg;

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
        zx_handle_t inferior, bool expected, const char* msg)
{
    zx_info_process_t info;
    // ZX_ASSERT returns false if the check fails.
    ASSERT_EQ(zx_object_get_info(
            inferior, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL), ZX_OK, "");
    ASSERT_EQ(info.debugger_attached, expected, msg);
    return true;
}

// This returns a bool as it's a unittest "helper" routine.
// N.B. This runs on the wait-inferior thread.

static bool handle_thread_exiting(zx_handle_t inferior,
                                  const zx_port_packet_t* packet)
{
    BEGIN_HELPER;

    zx_koid_t tid = packet->exception.tid;
    zx_handle_t thread;
    zx_status_t status = zx_object_get_child(inferior, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    // If the process has exited then the kernel may have reaped the
    // thread already. Check.
    if (status != ZX_ERR_NOT_FOUND) {
        zx_info_thread_t info = tu_thread_get_info(thread);
        // The thread could still transition to DEAD here (if the
        // process exits), so check for either DYING or DEAD.
        EXPECT_TRUE(info.state == ZX_THREAD_STATE_DYING ||
                    info.state == ZX_THREAD_STATE_DEAD, "");
        // If the state is DYING it would be nice to check that the
        // value of |info.wait_exception_port_type| is DEBUGGER. Alas
        // if the process has exited then the thread will get
        // THREAD_SIGNAL_KILL which will cause
        // UserThread::ExceptionHandlerExchange to exit before we've
        // told the thread to "resume" from ZX_EXCP_THREAD_EXITING.
        // The thread is still in the DYING state but it is no longer
        // in an exception. Thus |info.wait_exception_port_type| can
        // either be DEBUGGER or NONE.
        EXPECT_TRUE(info.wait_exception_port_type == ZX_EXCEPTION_PORT_TYPE_NONE ||
                    info.wait_exception_port_type == ZX_EXCEPTION_PORT_TYPE_DEBUGGER, "");
        tu_handle_close(thread);
    } else {
        EXPECT_TRUE(tu_process_has_exited(inferior), "");
    }
    unittest_printf("wait-inf: thread %" PRId64 " exited\n", tid);
    // A thread is gone, but we only care about the process.
    if (!resume_inferior(inferior, tid))
        return false;

    END_HELPER;
}

// This returns a bool as it's a unittest "helper" routine.
// N.B. This runs on the wait-inferior thread.

static bool handle_expected_page_fault(zx_handle_t inferior,
                                       const zx_port_packet_t* packet,
                                       atomic_int* segv_count)
{
    BEGIN_HELPER;

    unittest_printf("wait-inf: got page fault exception\n");

    zx_koid_t tid = packet->exception.tid;
    zx_handle_t thread = tu_get_thread(inferior, tid);

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
    atomic_fetch_add(segv_count, 1);

    zx_status_t status = zx_task_resume(thread, ZX_RESUME_EXCEPTION);
    tu_handle_close(thread);
    ASSERT_EQ(status, ZX_OK, "");

    END_HELPER;
}

// N.B. This runs on the wait-inferior thread.

static bool debugger_test_exception_handler(zx_handle_t inferior,
                                            const zx_port_packet_t* packet,
                                            void* handler_arg)
{
    BEGIN_HELPER;

    // Note: This may be NULL if the test is not expecting a page fault.
    atomic_int* segv_count = handler_arg;

    zx_koid_t tid = packet->exception.tid;

    switch (packet->type) {
        case ZX_EXCP_THREAD_STARTING:
            unittest_printf("wait-inf: inferior started\n");
            if (!resume_inferior(inferior, tid))
                return false;
            break;

        case ZX_EXCP_THREAD_EXITING:
            EXPECT_TRUE(handle_thread_exiting(inferior, packet), "");
            break;

        case ZX_EXCP_GONE:
            // A thread is gone, but we only care about the process which is
            // handled by the caller.
            break;

        case ZX_EXCP_FATAL_PAGE_FAULT:
            ASSERT_NONNULL(segv_count, "");
            ASSERT_TRUE(handle_expected_page_fault(inferior, packet, segv_count), "");
            break;

        default: {
            char msg[128];
            snprintf(msg, sizeof(msg), "unexpected packet type: 0x%x",
                     packet->type);
            ASSERT_TRUE(false, msg);
            __UNREACHABLE;
        }
    }

    END_HELPER;
}

static bool debugger_test(void)
{
    BEGIN_TEST;

    launchpad_t* lp;
    zx_handle_t inferior, channel;
    if (!setup_inferior(test_inferior_child_name, &lp, &inferior, &channel))
        return false;

    atomic_int segv_count;

    expect_debugger_attached_eq(inferior, false, "debugger should not appear attached");
    zx_handle_t eport = ZX_HANDLE_INVALID;
    thrd_t wait_inf_thread =
        start_wait_inf_thread(inferior, &eport,
                              debugger_test_exception_handler, &segv_count);
    EXPECT_NE(eport, ZX_HANDLE_INVALID, "");
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
    zx_handle_t inferior, channel;
    if (!setup_inferior(test_inferior_child_name, &lp, &inferior, &channel))
        return false;

    zx_handle_t eport = ZX_HANDLE_INVALID;
    thrd_t wait_inf_thread =
        start_wait_inf_thread(inferior, &eport,
                              debugger_test_exception_handler, NULL);
    EXPECT_NE(eport, ZX_HANDLE_INVALID, "");

    if (!start_inferior(lp))
        return false;
    if (!verify_inferior_running(channel))
        return false;

    enum message msg;
    send_msg(channel, MSG_START_EXTRA_THREADS);
    if (!recv_msg(channel, &msg))
        return false;
    EXPECT_EQ(msg, MSG_EXTRA_THREADS_STARTED, "unexpected response when starting extra threads");

    uint32_t buf_size = 100 * sizeof(zx_koid_t);
    size_t num_threads;
    zx_koid_t* threads = tu_malloc(buf_size);
    zx_status_t status = zx_object_get_info(inferior, ZX_INFO_PROCESS_THREADS,
                                            threads, buf_size, &num_threads, NULL);
    ASSERT_EQ(status, ZX_OK, "");

    // There should be at least 1+NUM_EXTRA_THREADS threads in the result.
    ASSERT_GE(num_threads, (unsigned)(1 + NUM_EXTRA_THREADS), "zx_object_get_info failed");

    // Verify each entry is valid.
    for (uint32_t i = 0; i < num_threads; ++i) {
        zx_koid_t koid = threads[i];
        unittest_printf("Looking up thread %llu\n", (long long) koid);
        zx_handle_t thread = tu_get_thread(inferior, koid);
        zx_info_handle_basic_t info;
        status = zx_object_get_info(thread, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
        EXPECT_EQ(status, ZX_OK, "zx_object_get_info failed");
        EXPECT_EQ(info.type, (uint32_t) ZX_OBJ_TYPE_THREAD, "not a thread");
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

    zx_handle_t self = zx_process_self();

    // We shouldn't be able to set it.
    uintptr_t debug_addr = 42;
    zx_status_t status = zx_object_set_property(self, ZX_PROP_PROCESS_DEBUG_ADDR,
                                                &debug_addr, sizeof(debug_addr));
    ASSERT_EQ(status, ZX_ERR_ACCESS_DENIED, "");

    // Some minimal verification that the value is correct.

    status = zx_object_get_property(self, ZX_PROP_PROCESS_DEBUG_ADDR,
                                    &debug_addr, sizeof(debug_addr));
    ASSERT_EQ(status, ZX_OK, "");

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

    zx_handle_t self = zx_process_self();

    // Exercise ZX-739
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

// These are "call-saved" registers used in the test.
#ifdef __x86_64__
#define REG_ACCESS_TEST_REG_NAME "r15"
#define REG_ACCESS_TEST_REG_OFFSET offsetof(zx_x86_64_general_regs_t, r15)
#endif
#ifdef __aarch64__
#define REG_ACCESS_TEST_REG_NAME "x28"
#define REG_ACCESS_TEST_REG_OFFSET offsetof(zx_arm64_general_regs_t, r[28])
#endif

// Note: Neither of these can be zero.
static const uint64_t reg_access_initial_value = 0xee112233445566eeull;
static const uint64_t reg_access_write_test_value = 0xee665544332211eeull;

struct suspended_reg_access_arg {
    zx_handle_t channel;
    uint64_t initial_value;
    uint64_t result;
    uint64_t pc, sp;
};

static int reg_access_thread_func(void* arg_)
{
    struct suspended_reg_access_arg* arg = arg_;

    send_msg(arg->channel, MSG_PONG);

    // The loop has to be written in assembler as we cannot control what
    // the compiler does with our "reserved" registers outside of the asm;
    // they're not really reserved in the way we need them to be: the compiler
    // is free to do with them whatever it wants outside of the assembler.
    // We do make the assumption that test_reg will not contain
    // |reg_access_initial_value| until it is set by the assembler.

    uint64_t initial_value = arg->initial_value;
    uint64_t result = 0;
    uint64_t pc = 0;
    uint64_t sp = 0;

// The maximum number of bytes in the assembly.
// This doesn't have to be perfect. It's used to verify the value read for
// $pc is within some reasonable range.
#define REG_ACCESS_MAX_LOOP_SIZE 64

#ifdef __x86_64__
    __asm__("\
        call 1f\n\
      1:\n\
        pop %[pc]\n\
        mov %%rsp, %[sp]\n\
        mov %[initial_value], %%" REG_ACCESS_TEST_REG_NAME "\n\
      2:\n\
        pause\n\
        cmp %[initial_value], %%" REG_ACCESS_TEST_REG_NAME "\n\
        je 2b\n\
        mov %%" REG_ACCESS_TEST_REG_NAME ", %[result]"
            : [result] "=r" (result),
              [pc] "=r" (pc),
              [sp] "=r" (sp)
            : [initial_value] "r" (initial_value)
            : REG_ACCESS_TEST_REG_NAME);
#endif

#ifdef __aarch64__
    __asm__("\
        adr %[pc], .\n\
        mov %[sp], sp\n\
        mov " REG_ACCESS_TEST_REG_NAME ", %[initial_value]\n\
      1:\n\
        yield\n\
        cmp %[initial_value], " REG_ACCESS_TEST_REG_NAME "\n\
        b.eq 1b\n\
        mov %[result], " REG_ACCESS_TEST_REG_NAME
            : [result] "=r" (result),
              [pc] "=r" (pc),
              [sp] "=r" (sp)
            : [initial_value] "r" (initial_value)
            : REG_ACCESS_TEST_REG_NAME);
#endif

    arg->result = result;
    arg->pc = pc;
    arg->sp = sp;

    tu_handle_close(arg->channel);

    return 0;
}

static bool suspended_reg_access_test(void)
{
    BEGIN_TEST;

    zx_handle_t self_proc = zx_process_self();

    thrd_t thread_c11;
    struct suspended_reg_access_arg arg = {};
    arg.initial_value = reg_access_initial_value;
    zx_handle_t channel;
    tu_channel_create(&channel, &arg.channel);
    tu_thread_create_c11(&thread_c11, reg_access_thread_func,
                         &arg, "reg-access thread");
    zx_handle_t thread = thrd_get_zx_handle(thread_c11);

    // KISS: Don't attach until the thread is up and running so we don't see
    // ZX_EXCP_THREAD_STARTING.
    enum message msg;
    recv_msg(channel, &msg);
    // No need to send a ping.
    ASSERT_EQ(msg, MSG_PONG, "");

    // Attach to debugger port so we can see ZX_EXCP_THREAD_SUSPENDED.
    // Don't do this until now so that we don't have to process things like
    // ZX_EXCP_THREAD_STARTING. OTOH, we might still get ZX_EXCP_THREAD_EXITING
    // from previous tests. See wait_thread_suspended.
    zx_handle_t eport = attach_inferior(self_proc);

    const size_t test_reg_offset = REG_ACCESS_TEST_REG_OFFSET;

    // Keep looping until we know the thread is stopped in the assembler.
    // This is the only place we can guarantee particular registers have
    // particular values.
    uint64_t test_reg = 0;
    while (test_reg != reg_access_initial_value) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
        ASSERT_EQ(zx_task_suspend(thread), ZX_OK, "");
        ASSERT_TRUE(wait_thread_suspended(self_proc, thread, eport), "");
        test_reg = get_uint64_register(thread, test_reg_offset);
    }

    uint64_t pc_value = get_uint64_register(thread, PC_REG_OFFSET);
    uint64_t sp_value = get_uint64_register(thread, SP_REG_OFFSET);

    set_uint64_register(thread, test_reg_offset, reg_access_write_test_value);

    ASSERT_EQ(zx_task_resume(thread, 0), ZX_OK, "");
    thrd_join(thread_c11, NULL);

    // We can't test the pc value exactly as we don't know on which instruction
    // the thread will be suspended. But we can verify it is within some
    // minimal range.
    EXPECT_GE(pc_value, arg.pc, "");
    EXPECT_LE(pc_value, arg.pc + REG_ACCESS_MAX_LOOP_SIZE, "");

    EXPECT_EQ(sp_value, arg.sp, "");

    EXPECT_EQ(reg_access_write_test_value, arg.result, "");

    tu_handle_close(channel);
    tu_handle_close(eport);
    END_TEST;
}

struct suspended_in_syscall_reg_access_arg {
    bool do_channel_call;
    zx_handle_t syscall_handle;
    atomic_uintptr_t sp;
};

// "zx_channel_call treats the leading bytes of the payload as
// a transaction id of type zx_txid_t"
static_assert(sizeof(zx_txid_t) == sizeof(uint32_t), "");
#define CHANNEL_CALL_PACKET_SIZE (sizeof(zx_txid_t) + sizeof("x"))

static int suspended_in_syscall_reg_access_thread_func(void* arg_)
{
    struct suspended_in_syscall_reg_access_arg* arg = arg_;

    uint64_t sp;
#ifdef __x86_64__
    __asm__("\
        mov %%rsp, %[sp]"
            : [sp] "=r" (sp));
#endif
#ifdef __aarch64__
    __asm__("\
        mov %[sp], sp"
            : [sp] "=r" (sp));
#endif
    atomic_store(&arg->sp, sp);

    if (arg->do_channel_call) {
        uint8_t send_buf[CHANNEL_CALL_PACKET_SIZE] = "TXIDx";
        uint8_t recv_buf[CHANNEL_CALL_PACKET_SIZE];
        uint32_t actual_bytes, actual_handles;
        zx_channel_call_args_t call_args = {
            .wr_bytes = send_buf,
            .wr_handles = NULL,
            .rd_bytes = recv_buf,
            .rd_handles = NULL,
            .wr_num_bytes = sizeof(send_buf),
            .wr_num_handles = 0,
            .rd_num_bytes = sizeof(recv_buf),
            .rd_num_handles = 0,
        };
        zx_status_t call_status =
            zx_channel_call(arg->syscall_handle, 0, ZX_TIME_INFINITE,
                            &call_args, &actual_bytes, &actual_handles,
                            NULL);
        ASSERT_EQ(call_status, ZX_OK, "");
        EXPECT_EQ(actual_bytes, sizeof(recv_buf), "");
        EXPECT_EQ(memcmp(recv_buf, "TXIDy", sizeof(recv_buf)), 0, "");
    } else {
        zx_signals_t pending;
        zx_status_t status =
            zx_object_wait_one(arg->syscall_handle, ZX_EVENT_SIGNALED,
                               ZX_TIME_INFINITE, &pending);
        ASSERT_EQ(status, ZX_OK, "");
        EXPECT_NE(pending & ZX_EVENT_SIGNALED, 0u, "");
    }

    return 0;
}

// Channel calls are a little special in that they are a two part syscall,
// with suspension possible in between the two parts.
// If |do_channel_call| is true, test zx_channel_call. Otherwise test some
// random syscall that can block, here we use zx_object_wait_one.
//
// The syscall entry point is the vdso, there's no bypassing this for test
// purposes. Also, the kernel doesn't save userspace regs on entry, it only
// saves them later if it needs to - at which point many don't necessarily
// have any useful value. Putting these together means we can't easily test
// random integer registers: there's no guarantee any value we set in the test
// will be available when the syscall is suspended. All is not lost, we can
// still at least test that reading $pc, $sp work.

static bool suspended_in_syscall_reg_access_worker(bool do_channel_call)
{
    zx_handle_t self_proc = zx_process_self();

    uintptr_t vdso_start = 0, vdso_end = 0;
    EXPECT_TRUE(get_vdso_exec_range(&vdso_start, &vdso_end), "");

    struct suspended_in_syscall_reg_access_arg arg = {};
    arg.do_channel_call = do_channel_call;

    zx_handle_t syscall_handle;
    if (do_channel_call) {
        tu_channel_create(&arg.syscall_handle, &syscall_handle);
    } else {
        ASSERT_EQ(zx_event_create(0u, &syscall_handle), ZX_OK, "");
        arg.syscall_handle = syscall_handle;
    }

    thrd_t thread_c11;
    tu_thread_create_c11(&thread_c11,
                         suspended_in_syscall_reg_access_thread_func,
                         &arg, "reg-access thread");
    zx_handle_t thread = thrd_get_zx_handle(thread_c11);

    // Busy-wait until thread is blocked inside the syscall.
    zx_info_thread_t thread_info;
    do {
        zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
        thread_info = tu_thread_get_info(thread);
    } while (thread_info.state != ZX_THREAD_STATE_BLOCKED);
    ASSERT_EQ(thread_info.wait_exception_port_type, ZX_EXCEPTION_PORT_TYPE_NONE, "");

    // Extra sanity check for channels.
    if (do_channel_call) {
        EXPECT_TRUE(tu_channel_wait_readable(syscall_handle), "");
    }

    // Attach to debugger port so we can see ZX_EXCP_THREAD_SUSPENDED.
    // Don't do this until now so that we don't have to process things like
    // ZX_EXCP_THREAD_STARTING. OTOH, we might still get ZX_EXCP_THREAD_EXITING
    // from previous tests. See wait_thread_suspended.
    zx_handle_t eport = attach_inferior(self_proc);

    ASSERT_EQ(zx_task_suspend(thread), ZX_OK, "");

    ASSERT_TRUE(wait_thread_suspended(self_proc, thread, eport), "");

    // Verify the pc is somewhere within the vdso.
    uint64_t pc_value = get_uint64_register(thread, PC_REG_OFFSET);
    EXPECT_GE(pc_value, vdso_start, "");
    EXPECT_LE(pc_value, vdso_end, "");

    // The stack pointer is somewhere within the syscall.
    // Just verify the value we have is within range.
    uint64_t sp_value = get_uint64_register(thread, SP_REG_OFFSET);
    uint64_t arg_sp = atomic_load(&arg.sp);
    EXPECT_LE(sp_value, arg_sp, "");
    EXPECT_GE(sp_value + 1024, arg_sp, "");

    // wake the thread
    if (do_channel_call) {
        uint8_t buf[CHANNEL_CALL_PACKET_SIZE];
        uint32_t actual_bytes;
        ASSERT_EQ(zx_channel_read(syscall_handle, 0, buf, NULL, sizeof(buf), 0, &actual_bytes, NULL),
                  ZX_OK, "");
        EXPECT_EQ(actual_bytes, sizeof(buf), "");
        EXPECT_EQ(memcmp(buf, "TXIDx", sizeof(buf)), 0, "");

        // write a reply
        buf[sizeof(zx_txid_t)] = 'y';
        ASSERT_EQ(zx_channel_write(syscall_handle, 0, buf, sizeof(buf), NULL, 0), ZX_OK, "");

        // Make sure the remote channel didn't get signaled
        EXPECT_EQ(zx_object_wait_one(arg.syscall_handle, ZX_CHANNEL_READABLE, 0, NULL),
                  ZX_ERR_TIMED_OUT, "");

        // Make sure we can't read from the remote channel (the message should have
        // been reserved for the other thread, even though it is suspended).
        EXPECT_EQ(zx_channel_read(arg.syscall_handle, 0, buf, NULL, sizeof(buf), 0,
                                  &actual_bytes, NULL),
                  ZX_ERR_SHOULD_WAIT, "");
    } else {
        ASSERT_EQ(zx_object_signal(syscall_handle, 0u, ZX_EVENT_SIGNALED), ZX_OK, "");
    }
    ASSERT_EQ(zx_task_resume(thread, 0), ZX_OK, "");

    thrd_join(thread_c11, NULL);

    tu_handle_close(eport);
    if (do_channel_call) {
        tu_handle_close(arg.syscall_handle);
    }
    tu_handle_close(syscall_handle);

    return true;
}

static bool suspended_in_syscall_reg_access_test(void)
{
    BEGIN_TEST;

    EXPECT_TRUE(suspended_in_syscall_reg_access_worker(false), "");

    END_TEST;
}

static bool suspended_in_channel_call_reg_access_test(void)
{
    BEGIN_TEST;

    EXPECT_TRUE(suspended_in_syscall_reg_access_worker(true), "");

    END_TEST;
}

struct suspend_in_exception_data {
    atomic_int segv_count;
    atomic_int suspend_count;
    atomic_int resume_count;
    zx_handle_t thread_handle;
    zx_koid_t thread_id;
};

// N.B. This runs on the wait-inferior thread.

static bool suspended_in_exception_handler(zx_handle_t inferior,
                                           const zx_port_packet_t* packet,
                                           void* handler_arg)
{
    BEGIN_HELPER;

    struct suspend_in_exception_data* data = handler_arg;
    zx_koid_t tid = packet->exception.tid;

    switch (packet->type) {
        case ZX_EXCP_THREAD_EXITING:
            EXPECT_TRUE(handle_thread_exiting(inferior, packet), "");
            break;

        case ZX_EXCP_THREAD_SUSPENDED:
            ASSERT_EQ(tid, data->thread_id, "");
            atomic_fetch_add(&data->suspend_count, 1);
            ASSERT_EQ(zx_task_resume(data->thread_handle, 0), ZX_OK, "");
            // At this point we should get ZX_EXCP_THREAD_RESUMED, we'll
            // process it later.
            break;

        case ZX_EXCP_THREAD_RESUMED:
            ASSERT_EQ(tid, data->thread_id, "");
            atomic_fetch_add(&data->resume_count, 1);
            break;

        case ZX_EXCP_FATAL_PAGE_FAULT: {
            unittest_printf("wait-inf: got page fault exception\n");

            ASSERT_EQ(tid, data->thread_id, "");

            // Verify that the fault is at the PC we expected.
            if (!test_segv_pc(data->thread_handle))
                return false;

            // Suspend the thread before fixing the segv to verify register
            // access works while the thread is in an exception and suspended.
            ASSERT_EQ(zx_task_suspend(data->thread_handle), ZX_OK, "");

            // Waiting for the thread to suspend doesn't work here as the
            // thread stays in the exception until we pass ZX_RESUME_EXCEPTION.
            // Just give the scheduler a chance to run the thread and process
            // the ZX_ERR_INTERNAL_INTR_RETRY in ExceptionHandlerExchange.
            zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));

            // Do some tests that require a suspended inferior.
            // This is required as the inferior does tests after it wakes up
            // that assumes we've done this.
            test_memory_ops(inferior, data->thread_handle);

            // Now correct the issue and resume the inferior.
            fix_inferior_segv(data->thread_handle);

            atomic_fetch_add(&data->segv_count, 1);

            ASSERT_EQ(zx_task_resume(data->thread_handle, ZX_RESUME_EXCEPTION), ZX_OK, "");
            // At this point we should get ZX_EXCP_THREAD_SUSPENDED, we'll
            // process it later.

            break;
        }

        default: {
            char msg[128];
            snprintf(msg, sizeof(msg), "unexpected packet type: 0x%x",
                     packet->type);
            ASSERT_TRUE(false, msg);
            __UNREACHABLE;
        }
    }

    END_HELPER;
}

static bool suspended_in_exception_reg_access_test(void)
{
    BEGIN_TEST;

    launchpad_t* lp;
    zx_handle_t inferior, channel;
    if (!setup_inferior(test_inferior_child_name, &lp, &inferior, &channel))
        return false;

    if (!start_inferior(lp))
        return false;
    if (!verify_inferior_running(channel))
        return false;

    struct suspend_in_exception_data data;
    atomic_store(&data.segv_count, 0);
    atomic_store(&data.suspend_count, 0);
    atomic_store(&data.resume_count, 0);
    ASSERT_TRUE(get_inferior_thread_handle(channel, &data.thread_handle), "");
    data.thread_id = tu_get_koid(data.thread_handle);

    // Defer attaching until now so that we don't have to handle
    // ZX_EXCP_THREAD_STARTING. OTOH, we might still get ZX_EXCP_THREAD_EXITING
    // from previous tests.
    zx_handle_t eport = ZX_HANDLE_INVALID;
    thrd_t wait_inf_thread =
        start_wait_inf_thread(inferior, &eport,
                              suspended_in_exception_handler, &data);
    EXPECT_GT(eport, 0, "");

    enum message msg;
    send_msg(channel, MSG_CRASH_AND_RECOVER_TEST);
    if (!recv_msg(channel, &msg)) {
        return false;
    }
    EXPECT_EQ(msg, MSG_RECOVERED_FROM_CRASH, "");

    if (!shutdown_inferior(channel, inferior))
        return false;

    // Stop the waiter thread before closing the eport that it's waiting on.
    join_wait_inf_thread(wait_inf_thread);

    // Don't check these until now to ensure the resume_count has been
    // updated (we're guaranteed that ZX_EXCP_THREAD_RESUMED will be sent
    // before ZX_EXCP_GONE for the process).
    EXPECT_EQ(atomic_load(&data.segv_count), NUM_SEGV_TRIES, "");
    EXPECT_EQ(atomic_load(&data.suspend_count), NUM_SEGV_TRIES, "");
    EXPECT_EQ(atomic_load(&data.resume_count), NUM_SEGV_TRIES, "");

    tu_handle_close(data.thread_handle);
    tu_handle_close(eport);
    tu_handle_close(channel);
    tu_handle_close(inferior);

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
        zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
    return 0;
}

// This returns a bool as it's a unittest "helper" routine.

static bool msg_loop(zx_handle_t channel)
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
            // Each will require an ZX_EXCP_STARTING exchange with the "debugger".
            while (atomic_load(&extra_thread_count) < NUM_EXTRA_THREADS)
                zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
            send_msg(channel, MSG_EXTRA_THREADS_STARTED);
            break;
        case MSG_GET_THREAD_HANDLE: {
            zx_handle_t self = zx_thread_self();
            zx_handle_t copy;
            zx_handle_duplicate(self, ZX_RIGHT_SAME_RIGHTS, &copy);
            // Note: The handle is transferred to the receiver.
            uint64_t data = MSG_THREAD_HANDLE;
            unittest_printf("sending handle %d message on channel %u\n", copy, channel);
            tu_channel_write(channel, 0, &data, sizeof(data), &copy, 1);
            break;
        }
        default:
            unittest_printf("unknown message received: %d\n", msg);
            break;
        }
    }

    END_HELPER;
}

void test_inferior(void)
{
    zx_handle_t channel = zx_get_startup_handle(PA_USER0);
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
RUN_TEST(suspended_reg_access_test)
RUN_TEST(suspended_in_syscall_reg_access_test)
RUN_TEST(suspended_in_channel_call_reg_access_test)
RUN_TEST(suspended_in_exception_reg_access_test)
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
