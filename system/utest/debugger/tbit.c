// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __x86_64__ // entire file

#include <inttypes.h>
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

#include "debugger.h"
#include "utils.h"

#define X86_FLAGS_TF (1<<8)

const char test_tbit_child_name[] = "tbit";

// We don't need to construct a real syscall here.
// We just need to execute the syscall insn.
#define SYSCALL_MAGIC 0xdeb0000

// .data is used for the location and name sections because they contain
// dynamic relocs.
#define LOCATION_SECTION ".data.tbit.locs"
#define NAME_SECTION ".data.tbit.names"
#define STRING_SECTION ".rodata.tbit.strings"

extern uint64_t stop_locations[] __attribute__((section(LOCATION_SECTION)));
extern const char* stop_names[] __attribute__((section(NAME_SECTION)));

#define DO_INSN(name, insn) \
    insn "\n" \
    ".Lafter_" name ":\n" \
    \
    ".pushsection " LOCATION_SECTION "\n" \
    ".8byte .Lafter_" name "\n" \
    ".popsection\n" \
    \
    ".pushsection " STRING_SECTION "\n" \
    ".Lstr_" name ":\n" \
    ".asciz \"" name "\"\n" \
    ".popsection\n" \
    \
    ".pushsection " NAME_SECTION "\n" \
    ".8byte .Lstr_" name "\n" \
    ".popsection\n"

__NO_INLINE static void tbit_sequence (void)
{
    // Note: Each name must be unique.
    __asm__ (
        ".pushsection " LOCATION_SECTION "\n"
        "stop_locations:\n"
        ".popsection\n"
        ".pushsection " NAME_SECTION "\n"
        "stop_names:\n"
        ".popsection\n"

        // We don't singlestep over this insn.
        // This is here as a s/w breakpoint test, and as a way
        // to run to start of the test.
        DO_INSN("int3", "int3")

        DO_INSN("nop", "nop")

        DO_INSN("syscall_setup", "movabs %[syscall],%%rax")
        DO_INSN("syscall", "syscall")

        DO_INSN("pushfq", "pushfq")
        DO_INSN("popfq", "popfq")
        DO_INSN("pop_nop", "nop")

        // TODO: More tests, including handling of program itself using TF.

        ".pushsection " LOCATION_SECTION "\n"
        ".8byte 0\n"
        ".popsection\n"

        : : [syscall] "i" ((uint64_t) SYSCALL_MAGIC << 32) : "rax", "memory", "cc"
        );
}

// This returns "bool" because it uses ASSERT_*.

static bool tbit_msg_loop(mx_handle_t channel)
{
    BEGIN_HELPER;

    bool my_done_tests = false;

    while (!my_done_tests)
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
        case MSG_START_TBIT_TEST:
            tbit_sequence();
            send_msg(channel, MSG_TBIT_TEST_DONE);
            break;
        default:
            unittest_printf("unknown message received: %d\n", msg);
            break;
        }
    }

    END_HELPER;
}

int child_test_tbit(void)
{
    mx_handle_t channel = mxio_get_startup_handle(MX_HND_TYPE_USER0);
    unittest_printf("test_tbit: got handle %d\n", channel);

    if (!tbit_msg_loop(channel))
        exit(20);

    unittest_printf("Tbit inferior done\n");
    return 1234;
}

static void set_tbit(mx_handle_t thread, bool value)
{
    unittest_printf("Setting tbit of thread %d to %d\n", thread, value);
    uint64_t rflags = get_uint64_register(thread, offsetof(mx_x86_64_general_regs_t, rflags));
    if (value)
        rflags |= X86_FLAGS_TF;
    else
        rflags &= ~(uint64_t) X86_FLAGS_TF;
    set_uint64_register(thread, offsetof(mx_x86_64_general_regs_t, rflags), rflags);
}

static bool step_n_insns(mx_handle_t inferior, mx_koid_t tid,
                         mx_handle_t eport, int nr_insns)
{
    for (int i = 0; i < nr_insns; ++i) {
        if (!resume_inferior(inferior, tid))
            return false;
        mx_koid_t tmp_tid;
        if (!read_and_verify_exception(eport, inferior, MX_EXCP_HW_BREAKPOINT, &tmp_tid))
            return false;
        ASSERT_EQ(tmp_tid, tid, "unexpected tid");
    }
    return true;
}

static bool step_and_verify(const char *name,
                            mx_handle_t inferior, mx_handle_t thread, mx_koid_t tid,
                            mx_handle_t eport, uint64_t expected_pc)
{
    if (!step_n_insns(inferior, tid, eport, 1))
        return false;
    uint64_t pc = get_uint64_register(thread, offsetof(mx_x86_64_general_regs_t, rip));
    unittest_printf("step_and_verify, stopped at 0x%" PRIx64 "\n", pc);
    EXPECT_EQ(pc, expected_pc, name);
    return true;
}

static bool tbit_test (void)
{
    mx_koid_t tid, tmp_tid;

    BEGIN_TEST;

    launchpad_t* lp;
    mx_handle_t channel, inferior, eport;
    if (!setup_inferior(test_tbit_child_name, &lp, &inferior, &channel))
        return false;
    eport = attach_inferior(inferior);
    if (!start_inferior(lp))
        return false;

    if (!read_and_verify_exception(eport, inferior, MX_EXCP_START, &tid))
        return false;
    if (!resume_inferior(inferior, tid))
        return false;
    mx_handle_t thread = tu_get_thread(inferior, tid);

    if (!verify_inferior_running(channel))
        return false;

    enum message msg;
    send_msg(channel, MSG_START_TBIT_TEST);

    int nr_insns;
    for (nr_insns = 0; stop_locations[nr_insns] != 0; ++nr_insns)
        continue;
    int cur_insn_nr = 0;

    // Process the s/w bkpt insn.
    if (!read_and_verify_exception(eport, inferior, MX_EXCP_SW_BREAKPOINT, &tmp_tid))
        return false;
    ASSERT_EQ(tmp_tid, tid, "unexpected tid");
    uint64_t pc = get_uint64_register(thread, offsetof(mx_x86_64_general_regs_t, rip));
    EXPECT_EQ(pc, stop_locations[cur_insn_nr], stop_names[cur_insn_nr]);
    ++cur_insn_nr;

    set_tbit(thread, true);

    while (cur_insn_nr < nr_insns) {
        if (!step_and_verify(stop_names[cur_insn_nr], inferior, thread, tid,
                             eport, stop_locations[cur_insn_nr]))
            return false;
        ++cur_insn_nr;
    }

    // Done with tbit-stepping.
    set_tbit(thread, false);

    if (!resume_inferior(inferior, tid))
        return false;
    if (!recv_msg(channel, &msg))
        return false;
    EXPECT_EQ(msg, MSG_TBIT_TEST_DONE, "unexpected response starting tbit test");

    if (!shutdown_inferior(channel, inferior))
        return false;

    // Two "gone" messages: one for thread and one for process.
    if (!read_and_verify_exception(eport, inferior, MX_EXCP_GONE, &tmp_tid))
        return false;
    EXPECT_EQ(tmp_tid, tid, "unexpected tid");
    if (!read_and_verify_exception(eport, inferior, MX_EXCP_GONE, &tmp_tid))
        return false;
    EXPECT_EQ(tmp_tid, 0u, "unexpected tid");

    END_TEST;
}

BEGIN_TEST_CASE(tbit_tests)
RUN_TEST(tbit_test);
END_TEST_CASE(tbit_tests)

#endif // __x86_64__
