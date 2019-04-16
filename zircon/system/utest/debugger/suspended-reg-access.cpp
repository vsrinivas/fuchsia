// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <atomic>
#include <inttypes.h>
#include <link.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <lib/backtrace-request/backtrace-request.h>
#include <lib/zx/thread.h>
#include <pretty/hexdump.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include "crash-and-recover.h"
#include "debugger.h"
#include "inferior.h"
#include "inferior-control.h"
#include "utils.h"

namespace {

// These are "call-saved" registers used in the test.
#if defined(__x86_64__)
#define REG_ACCESS_TEST_REG r15
#define REG_ACCESS_TEST_REG_NAME "r15"
#elif defined(__aarch64__)
#define REG_ACCESS_TEST_REG r[28]
#define REG_ACCESS_TEST_REG_NAME "x28"
#endif

// Note: Neither of these can be zero.
const uint64_t reg_access_initial_value = 0xee112233445566eeull;
const uint64_t reg_access_write_test_value = 0xee665544332211eeull;

struct suspended_reg_access_arg_t {
    zx_handle_t channel;
    uint64_t initial_value;
    uint64_t result;
    uint64_t pc, sp;
};

int reg_access_thread_func(void* arg_) {
    auto arg = reinterpret_cast<suspended_reg_access_arg_t*>(arg_);

    send_simple_response(arg->channel, RESP_PONG);

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
        lea .(%%rip), %[pc]\n\
        mov %%rsp, %[sp]\n\
        mov %[initial_value], %%" REG_ACCESS_TEST_REG_NAME "\n\
      2:\n\
        pause\n\
        cmp %[initial_value], %%" REG_ACCESS_TEST_REG_NAME "\n\
        je 2b\n\
        mov %%" REG_ACCESS_TEST_REG_NAME ", %[result]"
            : [result] "=r"(result), [pc] "=&r"(pc), [sp] "=&r"(sp)
            : [initial_value] "r"(initial_value)
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
            : [result] "=r"(result), [pc] "=&r"(pc), [sp] "=&r"(sp)
            : [initial_value] "r"(initial_value)
            : REG_ACCESS_TEST_REG_NAME);
#endif

    arg->result = result;
    arg->pc = pc;
    arg->sp = sp;

    tu_handle_close(arg->channel);

    return 0;
}

bool SuspendedRegAccessTest() {
    BEGIN_TEST;

    zx_handle_t self_proc = zx_process_self();

    thrd_t thread_c11;
    suspended_reg_access_arg_t arg = {};
    arg.initial_value = reg_access_initial_value;
    zx_handle_t channel;
    tu_channel_create(&channel, &arg.channel);
    tu_thread_create_c11(&thread_c11, reg_access_thread_func, &arg, "reg-access thread");
    // Get our own copy of the thread handle to avoid lifetime issues of
    // thrd's copy.
    zx_handle_t thread = tu_handle_duplicate(thrd_get_zx_handle(thread_c11));

    // KISS: Don't attach until the thread is up and running so we don't see
    // ZX_EXCP_THREAD_STARTING.
    ASSERT_TRUE(recv_simple_response(channel, RESP_PONG), "");

    zx_handle_t eport = tu_io_port_create();

    // Keep looping until we know the thread is stopped in the assembler.
    // This is the only place we can guarantee particular registers have
    // particular values.
    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    zx_thread_state_general_regs_t regs;
    uint64_t test_reg = 0;
    while (true) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
        ASSERT_EQ(zx_task_suspend_token(thread, &suspend_token), ZX_OK);
        ASSERT_TRUE(wait_thread_suspended(self_proc, thread, eport));

        read_inferior_gregs(thread, &regs);
        test_reg = regs.REG_ACCESS_TEST_REG;

        if (test_reg == reg_access_initial_value)
            break;  // Keep thread suspended.

        // Resume and try again.
        zx_handle_close(suspend_token);
    }

    uint64_t pc_value = extract_pc_reg(&regs);
    uint64_t sp_value = extract_sp_reg(&regs);
    regs.REG_ACCESS_TEST_REG = reg_access_write_test_value;
    write_inferior_gregs(thread, &regs);

    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    thrd_join(thread_c11, NULL);
    tu_handle_close(thread);

    // We can't test the pc value exactly as we don't know on which instruction
    // the thread will be suspended. But we can verify it is within some
    // minimal range.
    EXPECT_GE(pc_value, arg.pc);
    EXPECT_LE(pc_value, arg.pc + REG_ACCESS_MAX_LOOP_SIZE);

    EXPECT_EQ(sp_value, arg.sp);

    EXPECT_EQ(reg_access_write_test_value, arg.result);

    tu_handle_close(channel);
    tu_handle_close(eport);
    END_TEST;
}

struct suspended_in_syscall_reg_access_arg_t {
    bool do_channel_call;
    zx_handle_t syscall_handle;
    std::atomic<uintptr_t> sp;
};

// "zx_channel_call treats the leading bytes of the payload as
// a transaction id of type zx_txid_t"
static_assert(sizeof(zx_txid_t) == sizeof(uint32_t), "");
#define CHANNEL_CALL_PACKET_SIZE (sizeof(zx_txid_t) + sizeof("x"))

// A helper function for |suspended_in_syscall_reg_access_thread_func()|
// so we can use ASSERT_*/EXPECT_*.

bool suspended_in_syscall_reg_access_thread_func_helper(
        suspended_in_syscall_reg_access_arg_t* arg) {
    BEGIN_HELPER;

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
        zx_status_t call_status = zx_channel_call(arg->syscall_handle, 0, ZX_TIME_INFINITE,
                                                  &call_args, &actual_bytes, &actual_handles);
        ASSERT_EQ(call_status, ZX_OK);
        EXPECT_EQ(actual_bytes, sizeof(recv_buf));
        EXPECT_EQ(memcmp(recv_buf + sizeof(zx_txid_t), "y", sizeof(recv_buf) - sizeof(zx_txid_t)), 0);
    } else {
        zx_signals_t pending;
        zx_status_t status =
            zx_object_wait_one(arg->syscall_handle, ZX_EVENT_SIGNALED, ZX_TIME_INFINITE, &pending);
        ASSERT_EQ(status, ZX_OK);
        ASSERT_NE((pending & ZX_EVENT_SIGNALED), 0u);
    }

    END_HELPER;
}

int suspended_in_syscall_reg_access_thread_func(void* arg_) {
    auto arg = reinterpret_cast<suspended_in_syscall_reg_access_arg_t*>(arg_);

    uint64_t sp;
#ifdef __x86_64__
    __asm__("\
        mov %%rsp, %[sp]"
            : [sp] "=r"(sp));
#endif
#ifdef __aarch64__
    __asm__("\
        mov %[sp], sp"
            : [sp] "=r"(sp));
#endif
    arg->sp.store(sp);

    if (!suspended_in_syscall_reg_access_thread_func_helper(arg)) {
        return -1;
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

bool suspended_in_syscall_reg_access_worker(bool do_channel_call) {
    BEGIN_HELPER;

    zx_handle_t self_proc = zx_process_self();

    uintptr_t vdso_start = 0, vdso_end = 0;
    EXPECT_TRUE(get_vdso_exec_range(&vdso_start, &vdso_end));

    suspended_in_syscall_reg_access_arg_t arg = {};
    arg.do_channel_call = do_channel_call;

    zx_handle_t syscall_handle;
    if (do_channel_call) {
        tu_channel_create(&arg.syscall_handle, &syscall_handle);
    } else {
        ASSERT_EQ(zx_event_create(0u, &syscall_handle), ZX_OK);
        arg.syscall_handle = syscall_handle;
    }

    thrd_t thread_c11;
    tu_thread_create_c11(&thread_c11, suspended_in_syscall_reg_access_thread_func, &arg,
                         "reg-access thread");
    // Get our own copy of the thread handle to avoid lifetime issues of
    // thrd's copy.
    zx_handle_t thread = tu_handle_duplicate(thrd_get_zx_handle(thread_c11));

    // Busy-wait until thread is blocked inside the syscall.
    zx_info_thread_t thread_info;
    uint32_t expected_blocked_reason =
        do_channel_call ? ZX_THREAD_STATE_BLOCKED_CHANNEL : ZX_THREAD_STATE_BLOCKED_WAIT_ONE;
    do {
        // Don't check too frequently here as it can blow up tracing output
        // when debugging with kernel tracing turned on.
        zx_nanosleep(zx_deadline_after(ZX_USEC(100)));
        thread_info = tu_thread_get_info(thread);
    } while (thread_info.state != expected_blocked_reason);
    ASSERT_EQ(thread_info.wait_exception_port_type, ZX_EXCEPTION_PORT_TYPE_NONE);

    // Extra sanity check for channels.
    if (do_channel_call) {
        EXPECT_TRUE(tu_channel_wait_readable(syscall_handle));
    }

    zx_handle_t eport = tu_io_port_create();

    zx_handle_t token;
    ASSERT_EQ(zx_task_suspend_token(thread, &token), ZX_OK);

    ASSERT_TRUE(wait_thread_suspended(self_proc, thread, eport));

    zx_thread_state_general_regs_t regs;
    read_inferior_gregs(thread, &regs);

    // Verify the pc is somewhere within the vdso.
    uint64_t pc_value = extract_pc_reg(&regs);
    EXPECT_GE(pc_value, vdso_start);
    EXPECT_LE(pc_value, vdso_end);

    // The stack pointer is somewhere within the syscall.
    // Just verify the value we have is within range.
    uint64_t sp_value = extract_sp_reg(&regs);
    uint64_t arg_sp = arg.sp.load();
    EXPECT_LE(sp_value, arg_sp);
    EXPECT_GE(sp_value + 1024, arg_sp);

    // wake the thread
    if (do_channel_call) {
        uint8_t buf[CHANNEL_CALL_PACKET_SIZE];
        uint32_t actual_bytes;
        ASSERT_EQ(
            zx_channel_read(syscall_handle, 0, buf, NULL, sizeof(buf), 0, &actual_bytes, NULL),
            ZX_OK);
        EXPECT_EQ(actual_bytes, sizeof(buf));
        EXPECT_EQ(memcmp(buf + sizeof(zx_txid_t), "x", sizeof(buf) - sizeof(zx_txid_t)), 0);

        // write a reply
        buf[sizeof(zx_txid_t)] = 'y';
        ASSERT_EQ(zx_channel_write(syscall_handle, 0, buf, sizeof(buf), NULL, 0), ZX_OK);

        // Make sure the remote channel didn't get signaled
        EXPECT_EQ(zx_object_wait_one(arg.syscall_handle, ZX_CHANNEL_READABLE, 0, NULL),
                  ZX_ERR_TIMED_OUT);

        // Make sure we can't read from the remote channel (the message should have
        // been reserved for the other thread, even though it is suspended).
        EXPECT_EQ(
            zx_channel_read(arg.syscall_handle, 0, buf, NULL, sizeof(buf), 0, &actual_bytes, NULL),
            ZX_ERR_SHOULD_WAIT);
    } else {
        ASSERT_EQ(zx_object_signal(syscall_handle, 0u, ZX_EVENT_SIGNALED), ZX_OK);
    }

    ASSERT_EQ(zx_handle_close(token), ZX_OK);
    int thread_result = -1;
    EXPECT_EQ(thrd_join(thread_c11, &thread_result), thrd_success);
    EXPECT_EQ(thread_result, 0);
    tu_handle_close(thread);

    tu_handle_close(eport);
    if (do_channel_call) {
        tu_handle_close(arg.syscall_handle);
    }
    tu_handle_close(syscall_handle);

    END_HELPER;
}

bool SuspendedInSyscallRegAccessTest() {
    BEGIN_TEST;

    EXPECT_TRUE(suspended_in_syscall_reg_access_worker(false));

    END_TEST;
}

bool SuspendedInChannelCallRegAccessTest() {
    BEGIN_TEST;

    EXPECT_TRUE(suspended_in_syscall_reg_access_worker(true));

    END_TEST;
}

struct suspend_in_exception_data_t {
    std::atomic<int> segv_count;
    std::atomic<int> suspend_count;
    std::atomic<int> resume_count;
    zx_handle_t thread_handle;
    zx_handle_t suspend_token;
    zx_koid_t process_id;
    zx_koid_t thread_id;
};

// N.B. This runs on the wait-inferior thread.

bool suspended_in_exception_handler(zx_handle_t inferior, zx_handle_t port,
                                    const zx_port_packet_t* packet, void* handler_arg) {
    BEGIN_HELPER;

    auto data = reinterpret_cast<suspend_in_exception_data_t*>(handler_arg);

    if (ZX_PKT_IS_SIGNAL_ONE(packet->type)) {
        // Must be a signal on one of the threads.
        ASSERT_TRUE(packet->key != data->process_id);
        zx_koid_t pkt_tid = packet->key;

        // The following signals are expected here.  Note that
        // ZX_THREAD_RUNNING and ZX_THREAD_TERMINATED can be reported
        // together in the same zx_port_packet_t.
        if (packet->signal.observed & ZX_THREAD_TERMINATED) {
            // Nothing to do.
        }
        if (packet->signal.observed & ZX_THREAD_RUNNING) {
            ASSERT_EQ(pkt_tid, data->thread_id);
            atomic_fetch_add(&data->resume_count, 1);
        }
        if (packet->signal.observed & ZX_THREAD_SUSPENDED) {
            ASSERT_EQ(pkt_tid, data->thread_id);
            atomic_fetch_add(&data->suspend_count, 1);
            ASSERT_EQ(zx_handle_close(data->suspend_token), ZX_OK);
            // At this point we should get ZX_THREAD_RUNNING, we'll
            // process it later.
        }
    } else {
        ASSERT_TRUE(ZX_PKT_IS_EXCEPTION(packet->type));

        zx_koid_t pkt_tid = packet->exception.tid;

        switch (packet->type) {
        case ZX_EXCP_THREAD_EXITING:
            // N.B. We could get thread exiting messages from previous
            // tests.
            EXPECT_TRUE(handle_thread_exiting(inferior, port, packet));
            break;

        case ZX_EXCP_FATAL_PAGE_FAULT: {
            unittest_printf("wait-inf: got page fault exception\n");

            ASSERT_EQ(pkt_tid, data->thread_id);

            // Verify that the fault is at the PC we expected.
            if (!test_segv_pc(data->thread_handle))
                return false;

            // Suspend the thread before fixing the segv to verify register
            // access works while the thread is in an exception and suspended.
            ASSERT_EQ(zx_task_suspend_token(data->thread_handle, &data->suspend_token), ZX_OK);

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

            ASSERT_EQ(zx_task_resume_from_exception(data->thread_handle, port, 0), ZX_OK);
            // At this point we should get ZX_THREAD_SUSPENDED, we'll
            // process it later.

            break;
        }

        default: {
            char msg[128];
            snprintf(msg, sizeof(msg), "unexpected packet type: 0x%x", packet->type);
            ASSERT_TRUE(false, msg);
            __UNREACHABLE;
        }
        }
    }

    END_HELPER;
}

bool SuspendedInExceptionRegAccessTest() {
    BEGIN_TEST;

    launchpad_t* lp;
    zx_handle_t inferior, channel;
    if (!setup_inferior(kTestInferiorChildName, &lp, &inferior, &channel))
        return false;

    if (!start_inferior(lp))
        return false;
    if (!verify_inferior_running(channel))
        return false;

    suspend_in_exception_data_t data;
    data.segv_count.store(0);
    data.suspend_count.store(0);
    data.resume_count.store(0);
    ASSERT_TRUE(get_inferior_thread_handle(channel, &data.thread_handle));
    data.process_id = tu_get_koid(inferior);
    data.thread_id = tu_get_koid(data.thread_handle);

    // Defer attaching until after the inferior is running to test
    // attach_inferior's recording of existing threads. If that fails
    // it won't see thread suspended/running messages from the thread.
    zx_handle_t eport = tu_io_port_create();
    size_t max_threads = 10;
    inferior_data_t* inferior_data = attach_inferior(inferior, eport, max_threads);
    thrd_t wait_inf_thread =
        start_wait_inf_thread(inferior_data, suspended_in_exception_handler, &data);
    EXPECT_NE(eport, ZX_HANDLE_INVALID);

    send_simple_request(channel, RQST_CRASH_AND_RECOVER_TEST);
    // wait_inf_thread will process the crash and resume the inferior.
    EXPECT_TRUE(recv_simple_response(channel, RESP_RECOVERED_FROM_CRASH), "");

    if (!shutdown_inferior(channel, inferior))
        return false;

    // Stop the waiter thread before closing the eport that it's waiting on.
    join_wait_inf_thread(wait_inf_thread);

    detach_inferior(inferior_data, true);

    // Don't check these until now to ensure the resume_count has been
    // updated (we're guaranteed that ZX_THREAD_RUNNING will be signalled
    // and processed before the waiter thread exits.
    EXPECT_EQ(data.segv_count.load(), kNumSegvTries);
    EXPECT_EQ(data.suspend_count.load(), kNumSegvTries);
    // There's an initial "RUNNING" signal that the handler will see.
    // That is why we add one here.
    EXPECT_EQ(data.resume_count.load(), kNumSegvTries + 1);

    tu_handle_close(data.thread_handle);
    tu_handle_close(eport);
    tu_handle_close(channel);
    tu_handle_close(inferior);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(suspended_tests)
RUN_TEST(SuspendedRegAccessTest)
RUN_TEST(SuspendedInSyscallRegAccessTest)
RUN_TEST(SuspendedInChannelCallRegAccessTest)
RUN_TEST(SuspendedInExceptionRegAccessTest)
END_TEST_CASE(suspended_tests)
