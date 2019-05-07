// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "tests.h"

#include <arch/arch_ops.h>
#include <arch/mp.h>
#if defined(__x86_64__)
#include <arch/x86.h>
#endif
#include <lib/unittest/unittest.h>

static bool test_x64_msrs() {
    BEGIN_TEST;

#if defined(__x86_64__)

    arch_disable_ints();
    // Test read_msr for an MSR that is known to always exist on x64.
    uint64_t val = read_msr(X86_MSR_IA32_LSTAR);
    EXPECT_NE(val, 0ull, "");

    // Test write_msr to write that value back.
    write_msr(X86_MSR_IA32_LSTAR, val);
    arch_enable_ints();

    // Test read_msr_safe for an MSR that is known to not exist.
    // If read_msr_safe is busted, then this will #GP (panic).
    // TODO: Enable when the QEMU TCG issue is sorted (TCG never
    // generates a #GP on MSR access).
#ifdef DISABLED
    uint64_t bad_val;
    // AMD MSRC001_2xxx are only readable via Processor Debug.
    auto bad_status = read_msr_safe(0xC0012000, &bad_val);
    EXPECT_NE(bad_status, ZX_OK, "");
#endif

    // Test read_msr_on_cpu.
    uint64_t initial_fmask = read_msr(X86_MSR_IA32_FMASK);
    for (uint i = 0; i < arch_max_num_cpus(); i++) {
        if (!mp_is_cpu_online(i)) {
            continue;
        }
        uint64_t fmask = read_msr_on_cpu(/*cpu=*/i, X86_MSR_IA32_FMASK);
        EXPECT_EQ(initial_fmask, fmask, "");
    }

    // Test write_msr_on_cpu
    for (uint i = 0; i < arch_max_num_cpus(); i++) {
        if (!mp_is_cpu_online(i)) {
            continue;
        }
        write_msr_on_cpu(/*cpu=*/i, X86_MSR_IA32_FMASK, /*val=*/initial_fmask);
    }
#endif

    END_TEST;
}

static bool test_x64_msrs_k_commands() {
    BEGIN_TEST;

#if defined(__x86_64__)
    console_run_script_locked("cpu rdmsr 0 0x10");
#endif

    END_TEST;
}

UNITTEST_START_TESTCASE(x64_platform_tests)
UNITTEST("basic test of read/write MSR variants", test_x64_msrs)
UNITTEST("test k cpu rdmsr commands", test_x64_msrs_k_commands)
UNITTEST_END_TESTCASE(x64_platform_tests, "x64_platform_tests", "");
