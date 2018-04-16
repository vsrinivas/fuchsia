// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <cpuid.h>
#include <stdio.h>
#include <unittest/unittest.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>

enum class Instruction {
    SGDT,
    SIDT,
    SLDT,
    STR,
    SMSW,
    NOOP, // Used to ensure harness does not always report failure
    MOV_NONCANON,  // Used to ensure harness does not always report success
};

namespace {

bool is_umip_supported() {
    uint32_t eax, ebx, ecx, edx;
    if (__get_cpuid(7, &eax, &ebx, &ecx, &edx) != 1) {
        return false;
    }
    return ecx & (1u << 2);
}

// If this returns true, the instruction is expected to cause a #GP if it
// is executed.
bool isn_should_crash(Instruction isn) {
    switch (isn) {
        case Instruction::SGDT:
        case Instruction::SIDT:
        case Instruction::SLDT:
        case Instruction::STR:
        case Instruction::SMSW:
            // If UMIP is supported, the kernel should have turned it on.
            return is_umip_supported();
        case Instruction::NOOP: return false;
        case Instruction::MOV_NONCANON:  return true;
    }
    __builtin_trap();
}

void isn_thread_func(uintptr_t raw_isn, void* unused) {
    Instruction isn = static_cast<Instruction>(raw_isn);

    alignas(16) static uint8_t scratch_buf[16];

    switch (isn) {
        case Instruction::SGDT:
            __asm__ volatile ("sgdt %0" : "=m"(*scratch_buf));
            break;
        case Instruction::SIDT:
            __asm__ volatile ("sidt %0" : "=m"(*scratch_buf));
            break;
        case Instruction::SLDT:
            __asm__ volatile ("sldt %0" : "=m"(*scratch_buf));
            break;
        case Instruction::STR:
            __asm__ volatile ("str %0" : "=m"(*scratch_buf));
            break;
        case Instruction::SMSW:
            uint64_t msw;
            __asm__ volatile ("smsw %0" : "=r"(msw));
            break;
        case Instruction::NOOP:
            __asm__ volatile ("nop");
            break;
        case Instruction::MOV_NONCANON:
            // We use a non-canonical address in order to produce a #GP, which we
            // specifically want to test (as opposed to other fault types such as
            // page faults).
            uint8_t* v = reinterpret_cast<uint8_t*>(1ULL << 63);
            __asm__ volatile ("movq $0, %0" : "=m"(*v));
            break;
    }

    zx_thread_exit();
}

bool test_instruction(Instruction isn) {
    BEGIN_HELPER;

    zx::thread thread;
    ASSERT_EQ(zx::thread::create(zx::process::self(), "isn_probe", 9u, 0u, &thread), ZX_OK);

    alignas(16) static uint8_t thread_stack[128];
    uintptr_t entry = reinterpret_cast<uintptr_t>(&isn_thread_func);
    uintptr_t stack_top = reinterpret_cast<uintptr_t>(thread_stack + sizeof(thread_stack));

    zx::port port;
    ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

    ASSERT_EQ(thread.wait_async(port, 0, ZX_THREAD_TERMINATED, ZX_WAIT_ASYNC_ONCE), ZX_OK);
    ASSERT_EQ(zx_task_bind_exception_port(thread.get(), port.get(), 0, 0), ZX_OK);

    ASSERT_EQ(thread.start(entry, stack_top, static_cast<uintptr_t>(isn), 0), ZX_OK);

    // Wait for crash or thread completion.
    zx_port_packet_t packet;
    while (port.wait(zx::time::infinite(), &packet, 1) == ZX_OK) {
        if (ZX_PKT_IS_EXCEPTION(packet.type)) {
            zx_exception_report_t report;
            ASSERT_EQ(thread.get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, &report, sizeof(report),
                                      NULL, NULL), ZX_OK);
            ASSERT_EQ(thread.kill(), ZX_OK);
            ASSERT_TRUE(isn_should_crash(isn));
            // These instructions should cause a GPF
            ASSERT_EQ(report.header.type, ZX_EXCP_GENERAL);
            break;
        } else if (ZX_PKT_IS_SIGNAL_ONE(packet.type)) {
            if (packet.signal.observed & ZX_THREAD_TERMINATED) {
                // Thread terminated normally so the instruction did not crash
                ASSERT_FALSE(isn_should_crash(isn));
                break;
            }
        }
    }

    END_HELPER;
}

template <Instruction isn>
bool umip_test() {
    BEGIN_TEST;

    test_instruction(isn);

    END_TEST;
}

}

BEGIN_TEST_CASE(x86_umip_test)
RUN_TEST(umip_test<Instruction::SGDT>)
RUN_TEST(umip_test<Instruction::SIDT>)
RUN_TEST(umip_test<Instruction::SLDT>)
RUN_TEST(umip_test<Instruction::SMSW>)
RUN_TEST(umip_test<Instruction::STR>)
RUN_TEST(umip_test<Instruction::NOOP>)
RUN_TEST(umip_test<Instruction::MOV_NONCANON>)
END_TEST_CASE(x86_umip_test)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
