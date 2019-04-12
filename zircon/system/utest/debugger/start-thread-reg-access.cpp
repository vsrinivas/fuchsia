// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <fbl/algorithm.h>
#include <lib/zx/thread.h>
#include <pretty/hexdump.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>
#include <zircon/types.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/port.h>

#include "debugger.h"
#include "inferior.h"
#include "inferior-control.h"
#include "utils.h"

namespace {

constexpr uint64_t kMagicRegisterValue = 0x0123456789abcdefull;

// State that is maintained across the register access tests.

struct reg_access_test_state_t {
    // The PC of the first thread can't be validated until we can get the
    // inferior's libc load address. Save it here for later validation.
    zx_vaddr_t inferior_libc_entry_point;

    // The load addresses of libc and executable are obtained from the
    // inferior after it has started.
    zx_vaddr_t inferior_libc_load_addr;
    zx_vaddr_t inferior_exec_load_addr;
};

typedef void (raw_thread_func_t)(void* arg1, void* arg2);

// Worker thread entry point so that we can exercise the setting of register
// values. We want to grab the register values at the start of the thread to
// see if they were set correctly, but we can't (or at least shouldn't) make
// any assumptions about what libc's thread entry will do to them before we're
// able to see them.

__NO_RETURN void raw_capture_regs_thread_func(void* arg1, void* arg2,
                                              raw_thread_func_t* func,
                                              uint64_t magic_value) {
    // We can't do much in this function, at this point all we have is a
    // raw thread. If |magic_value| is wrong then crash.
    if (magic_value != kMagicRegisterValue) {
        undefined_insn();
    }
    func(arg1, arg2);
    __UNREACHABLE;
}

// Helper function to test register access when a thread starts.

bool test_thread_start_register_access(reg_access_test_state_t* test_state,
                                       zx_handle_t inferior, zx_koid_t tid) {
    BEGIN_HELPER;

    zx::thread thread{tu_process_get_thread(inferior, tid)};
    ASSERT_TRUE(thread.is_valid());

    zx_info_thread_t info = tu_thread_get_info(thread.get());
    EXPECT_EQ(info.state, ZX_THREAD_STATE_BLOCKED_EXCEPTION, "");

    zx_thread_state_general_regs_t regs;
    read_inferior_gregs(thread.get(), &regs);
    uint64_t pc = extract_pc_reg(&regs);

    // If we're the first thread the pc should be the ELF entry point.
    // If not the pc should be the thread's entry point.
    zx_koid_t threads[1 + kNumExtraThreads];
    size_t num_threads = tu_process_get_threads(inferior, threads, fbl::count_of(threads));
    if (num_threads == 1) {
        // We don't know the inferior's load address yet so we can't do a full
        // validation of the PC yet. Save it for later when we can.
        test_state->inferior_libc_entry_point = pc;
    }

    // Verify the initial values of all the other general regs.
    zx_thread_state_general_regs_t expected_regs{};

    // We don't know what these are, but they're non-zero.
    // The rest are generally zero.
#if defined(__x86_64__)
    expected_regs.rip = regs.rip;
    expected_regs.rsp = regs.rsp;
    expected_regs.rdi = regs.rdi;
    expected_regs.rsi = regs.rsi;
#elif defined(__aarch64__)
    expected_regs.pc = regs.pc;
    expected_regs.sp = regs.sp;
    expected_regs.r[0] = regs.r[0];
    expected_regs.r[1] = regs.r[1];
#endif

    // These values we know with certainty.
    // See arch_setup_uspace_iframe().
#if defined(__x86_64__)

#define X86_FLAGS_IF (1 << 9)
#define X86_FLAGS_IOPL_SHIFT (12)
    expected_regs.rflags = (0 << X86_FLAGS_IOPL_SHIFT) | X86_FLAGS_IF;

#elif defined(__aarch64__)

#define ARM64_CPSR_MASK_SERROR (1UL << 8) 
    // TODO(dje): See TODO in arch_setup_uspace_iframe.
    // cpsr is read as 0x0 but it's set as 0x100;
    expected_regs.cpsr = regs.cpsr & ARM64_CPSR_MASK_SERROR;

#endif

    EXPECT_EQ(memcmp(&regs, &expected_regs, sizeof(regs)), 0);

    if (utest_verbosity_level >= 2) {
        printf("Got:\n");
        hexdump8_ex(&regs, sizeof(regs), 0);
        printf("Expected:\n");
        hexdump8_ex(&expected_regs, sizeof(expected_regs), 0);
    }

    // If this is one of the extra threads, redirect its entry point and
    // set additional registers for the thread to pick up.
    if (num_threads > 1) {
        EXPECT_NE(test_state->inferior_exec_load_addr, 0);
        zx_vaddr_t our_exec_load_addr = get_exec_load_addr();
        zx_vaddr_t raw_thread_func_addr =
            reinterpret_cast<zx_vaddr_t>(&raw_capture_regs_thread_func);
        raw_thread_func_addr -= our_exec_load_addr;
        raw_thread_func_addr += test_state->inferior_exec_load_addr;
#if defined(__x86_64__)
        regs.rdx = regs.rip;
        regs.rip = raw_thread_func_addr;
        regs.rcx = kMagicRegisterValue;
#elif defined(__aarch64__)
        regs.r[2] = regs.pc;
        regs.pc = raw_thread_func_addr;
        regs.r[3] = kMagicRegisterValue;
#endif
    }

    write_inferior_gregs(thread.get(), &regs);

    END_HELPER;
}

// N.B. This runs on the wait-inferior thread.

bool thread_start_test_exception_handler_worker(zx_handle_t inferior, zx_handle_t port,
                                                const zx_port_packet_t* packet,
                                                void* handler_arg) {
    BEGIN_HELPER;

    auto test_state = reinterpret_cast<reg_access_test_state_t*>(handler_arg);

    zx_koid_t pid = tu_get_koid(inferior);

    if (ZX_PKT_IS_SIGNAL_REP(packet->type)) {
        ASSERT_TRUE(packet->key != pid);
        // Must be a signal on one of the threads.
        // Here we're only expecting TERMINATED.
        ASSERT_TRUE(packet->signal.observed & ZX_THREAD_TERMINATED);
    } else {
        ASSERT_TRUE(ZX_PKT_IS_EXCEPTION(packet->type));

        zx_koid_t tid = packet->exception.tid;

        switch (packet->type) {
        case ZX_EXCP_THREAD_STARTING:
            unittest_printf("wait-inf: thread %lu started\n", tid);
            EXPECT_TRUE(test_thread_start_register_access(test_state, inferior, tid));
            if (!resume_inferior(inferior, port, tid))
                return false;
            break;

        case ZX_EXCP_THREAD_EXITING:
            EXPECT_TRUE(handle_thread_exiting(inferior, port, packet));
            break;

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

// N.B. This runs on the wait-inferior thread.

bool thread_start_test_exception_handler(zx_handle_t inferior,
                                         zx_handle_t port,
                                         const zx_port_packet_t* packet,
                                         void* handler_arg) {
    bool pass = thread_start_test_exception_handler_worker(inferior, port,
                                                           packet, handler_arg);

    // If a test failed detach now so that a thread isn't left waiting in
    // ZX_EXCP_THREAD_STARTING for a response.
    if (!pass) {
        unbind_inferior(inferior);
    }

    return pass;
}

} // namespace

int capture_regs_thread_func(void* arg) {
    auto thread_count_ptr = reinterpret_cast<std::atomic<int>*>(arg);
    atomic_fetch_add(thread_count_ptr, 1);
    unittest_printf("Extra thread started.\n");
    return 0;
}

bool StoppedInThreadStartingRegAccessTest() {
    BEGIN_TEST;

    launchpad_t* lp;
    zx_handle_t inferior, channel;
    if (!setup_inferior(kTestInferiorChildName, &lp, &inferior, &channel))
        return false;

    // Attach to the inferior now because we want to see thread starting
    // exceptions.
    zx_handle_t eport = tu_io_port_create();
    size_t max_threads = 10;
    inferior_data_t* inferior_data = attach_inferior(inferior, eport, max_threads);

    // State we need to maintain across the handling of the various exceptions.
    reg_access_test_state_t test_state{};

    thrd_t wait_inf_thread =
        start_wait_inf_thread(inferior_data, thread_start_test_exception_handler, &test_state);
    EXPECT_NE(eport, ZX_HANDLE_INVALID);

    if (!start_inferior(lp))
        return false;

    // The first test happens here as the main thread starts.
    // This testing is done in |thread_start_test_exception_handler()|.

    // Make sure the program successfully started.
    if (!verify_inferior_running(channel))
        return false;

    EXPECT_TRUE(get_inferior_load_addrs(channel,
                                        &test_state.inferior_libc_load_addr,
                                        &test_state.inferior_exec_load_addr), "");

    // Now that we have the inferior's libc load address we can verify the
    // executable's initial PC value (which is libc's entry point).
    // The inferior executable is us, so we can compute its entry point by
    // adding the offset of the entry point from our load address to the
    // inferior's load address.
    zx_vaddr_t expected_entry_point =
        test_state.inferior_libc_load_addr + get_libc_entry_point();
    EXPECT_EQ(test_state.inferior_libc_entry_point, expected_entry_point, "");

    send_simple_request(channel, RQST_START_LOOPING_THREADS);
    EXPECT_TRUE(recv_simple_response(channel, RESP_THREADS_STARTED), "");

    // The remaining testing happens at this point as threads start.
    // This testing is done in |thread_start_test_exception_handler()|.

    if (!shutdown_inferior(channel, inferior))
        return false;

    // Stop the waiter thread before closing the eport that it's waiting on.
    join_wait_inf_thread(wait_inf_thread);

    detach_inferior(inferior_data, true);

    tu_handle_close(eport);
    tu_handle_close(channel);
    tu_handle_close(inferior);

    END_TEST;
}

BEGIN_TEST_CASE(thread_start_tests)
RUN_TEST(StoppedInThreadStartingRegAccessTest)
END_TEST_CASE(thread_start_tests)
