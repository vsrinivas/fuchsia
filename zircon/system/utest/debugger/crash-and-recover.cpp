// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains basic "crash-and-recover" test support where the inferior
// crashes and then the cause of the crash is fixed in the debugger and then
// the inferior is resumed. The pieces of the test are abstracted out info
// this file as the test is done in a couple of places.
//
// The test consists of two parts:
// 1) Debugger side:
//      Send RQST_CRASH_AND_RECOVER.
//      In the exception handler:
//      - call TestSegvPc()
//      - call TestMemoryOps()
//      - call FixInferiorSegv()
//      - resume the inferior
// 2) Inferior side:
//      On receipt of RQST_CRASH_AND_RECOVER:
//      - call TestPrepAndSegv()
//      - send RESP_RECOVERED_FROM_CRASH

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

constexpr size_t kTestMemorySize = 8;
constexpr uint8_t kTestDataAdjust = 0x10;

} // namespace

bool test_prep_and_segv() {
    uint8_t test_data[kTestMemorySize];
    for (unsigned i = 0; i < sizeof(test_data); ++i)
        test_data[i] = static_cast<uint8_t>(i);

#ifdef __x86_64__
    void* segv_pc;
    // Note: Fuchsia is always PIC.
    __asm__("leaq .Lsegv_here(%%rip),%0" : "=r"(segv_pc));
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
"
            :
            : [zero] "g"(0), [test_data] "g"(&test_data[0]), [pc] "g"(segv_pc)
            : "rax", "r8", "r9", "r10");
#endif

#ifdef __aarch64__
    void* segv_pc;
    // Note: Fuchsia is always PIC.
    __asm__("adrp %0, .Lsegv_here\n"
            "add %0, %0, :lo12:.Lsegv_here"
            : "=r"(segv_pc));
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
"
            :
            : [test_data] "r"(&test_data[0]), [pc] "r"(segv_pc)
            : "x0", "x8", "x9", "x10");
#endif

    // On resumption test_data should have had kTestDataAdjust added to each element.
    // Note: This is the inferior process, it's not running under the test harness.
    for (unsigned i = 0; i < sizeof(test_data); ++i) {
        if (test_data[i] != i + kTestDataAdjust) {
            unittest_printf("TestPrepAndSegv: bad data on resumption, test_data[%u] = 0x%x\n", i,
                            test_data[i]);
            return false;
        }
    }

    unittest_printf("Inferior successfully resumed!\n");

    return true;
}

bool test_segv_pc(zx_handle_t thread) {
    BEGIN_HELPER;

    zx_thread_state_general_regs_t regs;
    read_inferior_gregs(thread, &regs);

#if defined(__x86_64__)
    ASSERT_EQ(regs.rip, regs.r10, "fault PC does not match r10");
#elif defined(__aarch64__)
    ASSERT_EQ(regs.pc, regs.r[10], "fault PC does not match x10");
#endif

    END_HELPER;
}

bool test_memory_ops(zx_handle_t inferior, zx_handle_t thread) {
    BEGIN_HELPER;

    uint64_t test_data_addr = 0;
    uint8_t test_data[kTestMemorySize];

    zx_thread_state_general_regs_t regs;
    read_inferior_gregs(thread, &regs);

#if defined(__x86_64__)
    test_data_addr = regs.r9;
#elif defined(__aarch64__)
    test_data_addr = regs.r[9];
#endif

    size_t size = read_inferior_memory(inferior, test_data_addr, test_data, sizeof(test_data));
    EXPECT_EQ(size, sizeof(test_data), "read_inferior_memory: short read");

    for (unsigned i = 0; i < sizeof(test_data); ++i) {
        EXPECT_EQ(test_data[i], i, "test_memory_ops");
    }

    for (unsigned i = 0; i < sizeof(test_data); ++i) {
        test_data[i] = static_cast<uint8_t>(test_data[i] + kTestDataAdjust);
    }

    size = write_inferior_memory(inferior, test_data_addr, test_data, sizeof(test_data));
    EXPECT_EQ(size, sizeof(test_data), "write_inferior_memory: short write");

    // Note: Verification of the write is done in the inferior.
    END_HELPER;
}

void fix_inferior_segv(zx_handle_t thread) {
    unittest_printf("Fixing inferior segv\n");

    // The segv was because r8 == 0, change it to a usable value. See TestPrepAndSegv.
    zx_thread_state_general_regs_t regs;
    read_inferior_gregs(thread, &regs);
#if defined(__x86_64__)
    regs.r8 = regs.rsp;
#elif defined(__aarch64__)
    regs.r[8] = regs.sp;
#endif
    write_inferior_gregs(thread, &regs);
}
