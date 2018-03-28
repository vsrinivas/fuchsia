// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014-2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch.h>
#include <arch/arm64.h>
#include <arch/arm64/feature.h>
#include <arch/arm64/mmu.h>
#include <arch/mp.h>
#include <arch/ops.h>
#include <assert.h>
#include <bits.h>
#include <debug.h>
#include <inttypes.h>
#include <kernel/cmdline.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <lk/main.h>
#include <platform.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#define LOCAL_TRACE 0

// Counter-timer Kernel Control Register, EL1.
static constexpr uint64_t CNTKCTL_EL1_ENABLE_VIRTUAL_COUNTER = 1 << 1;

// Monitor Debug System Control Register, EL1.
static constexpr uint32_t MDSCR_EL1_ENABLE_DEBUG_EXCEPTIONS = 1 << 13;
static constexpr uint32_t MDSCR_EL1_ENABLE_DEBUG_BREAKPOINTS = 1 << 15;

// Initial value for MSDCR_EL1 when starting userspace.
static constexpr uint32_t MSDCR_EL1_INITIAL_VALUE =
    MDSCR_EL1_ENABLE_DEBUG_EXCEPTIONS | MDSCR_EL1_ENABLE_DEBUG_BREAKPOINTS;

// Performance Monitors Count Enable Set, EL0.
static constexpr uint64_t PMCNTENSET_EL0_ENABLE = 1UL << 31;  // Enable cycle count register.

// Performance Monitor Control Register, EL0.
static constexpr uint64_t PMCR_EL0_ENABLE_BIT = 1 << 0;
static constexpr uint64_t PMCR_EL0_LONG_COUNTER_BIT = 1 << 6;

// Performance Monitors User Enable Regiser, EL0.
static constexpr uint64_t PMUSERENR_EL0_ENABLE = 1 << 0;  // Enable EL0 access to cycle counter.

// System Control Register, EL1.
static constexpr uint64_t SCTLR_EL1_UCI = 1 << 26; // Allow certain cache ops in EL0.
static constexpr uint64_t SCTLR_EL1_UCT = 1 << 15; // Allow EL0 access to CTR register.
static constexpr uint64_t SCTLR_EL1_DZE = 1 << 14; // Allow EL0 to use DC ZVA.
static constexpr uint64_t SCTLR_EL1_SA0 = 1 << 4;  // Enable Stack Alignment Check EL0.
static constexpr uint64_t SCTLR_EL1_SA = 1 << 3;   // Enable Stack Alignment Check EL1.
static constexpr uint64_t SCTLR_EL1_AC = 1 << 1;   // Enable Alignment Checking for EL1 EL0.

struct arm64_sp_info_t {
    uint64_t mpid;
    void* sp;

    // This part of the struct itself will serve temporarily as the
    // fake arch_thread in the thread pointer, so that safe-stack
    // and stack-protector code can work early.  The thread pointer
    // (TPIDR_EL1) points just past arm64_sp_info_t.
    uintptr_t stack_guard;
    void* unsafe_sp;
};

static_assert(sizeof(arm64_sp_info_t) == 32,
              "check arm64_get_secondary_sp assembly");
static_assert(offsetof(arm64_sp_info_t, sp) == 8,
              "check arm64_get_secondary_sp assembly");
static_assert(offsetof(arm64_sp_info_t, mpid) == 0,
              "check arm64_get_secondary_sp assembly");

#define TP_OFFSET(field) \
    ((int)offsetof(arm64_sp_info_t, field) - (int)sizeof(arm64_sp_info_t))
static_assert(TP_OFFSET(stack_guard) == ZX_TLS_STACK_GUARD_OFFSET, "");
static_assert(TP_OFFSET(unsafe_sp) == ZX_TLS_UNSAFE_SP_OFFSET, "");
#undef TP_OFFSET

// SMP boot lock.
static spin_lock_t arm_boot_cpu_lock = (spin_lock_t){1};
static volatile int secondaries_to_init = 0;
static thread_t _init_thread[SMP_MAX_CPUS - 1];
arm64_sp_info_t arm64_secondary_sp_list[SMP_MAX_CPUS];

extern uint64_t arch_boot_el;  // Defined in start.S.

uint64_t arm64_get_boot_el() {
    return arch_boot_el >> 2;
}

zx_status_t arm64_set_secondary_sp(uint cluster, uint cpu, void* sp, void* unsafe_sp) {
    uint64_t mpid = ARM64_MPID(cluster, cpu);

    uint32_t i = 0;
    while ((i < SMP_MAX_CPUS) && (arm64_secondary_sp_list[i].mpid != 0)) {
        i++;
    }
    if (i == SMP_MAX_CPUS)
        return ZX_ERR_NO_RESOURCES;
    LTRACEF("set mpid 0x%lx sp to %p\n", mpid, sp);
#if __has_feature(safe_stack)
    LTRACEF("set mpid 0x%lx unsafe-sp to %p\n", mpid, unsafe_sp);
#else
    DEBUG_ASSERT(unsafe_sp == NULL);
#endif
    arm64_secondary_sp_list[i].mpid = mpid;
    arm64_secondary_sp_list[i].sp = sp;
    arm64_secondary_sp_list[i].stack_guard = get_current_thread()->arch.stack_guard;
    arm64_secondary_sp_list[i].unsafe_sp = unsafe_sp;

    return ZX_OK;
}

static void arm64_cpu_early_init() {
    // Make sure the per cpu pointer is set up.
    arm64_init_percpu_early();

    // Set the vector base.
    ARM64_WRITE_SYSREG(VBAR_EL1, (uint64_t)&arm64_el1_exception_base);

    // Set some control bits in sctlr.
    uint64_t sctlr = ARM64_READ_SYSREG(sctlr_el1);
    sctlr |= SCTLR_EL1_UCI | SCTLR_EL1_UCT | SCTLR_EL1_DZE | SCTLR_EL1_SA0 | SCTLR_EL1_SA;
    sctlr &= ~SCTLR_EL1_AC;  // Disable alignment checking for EL1, EL0.
    ARM64_WRITE_SYSREG(sctlr_el1, sctlr);

    // Save all of the features of the cpu.
    arm64_feature_init();

    // Enable cycle counter.
    ARM64_WRITE_SYSREG(pmcr_el0, PMCR_EL0_ENABLE_BIT | PMCR_EL0_LONG_COUNTER_BIT);
    ARM64_WRITE_SYSREG(pmcntenset_el0, PMCNTENSET_EL0_ENABLE);

    // Enable user space access to cycle counter.
    ARM64_WRITE_SYSREG(pmuserenr_el0, PMUSERENR_EL0_ENABLE);

    // Enable user space access to virtual counter (CNTVCT_EL0).
    ARM64_WRITE_SYSREG(cntkctl_el1, CNTKCTL_EL1_ENABLE_VIRTUAL_COUNTER);

    ARM64_WRITE_SYSREG(mdscr_el1, MSDCR_EL1_INITIAL_VALUE);

    arch_enable_fiqs();
}

void arch_early_init() {
    arm64_cpu_early_init();

    platform_init_mmu_mappings();
}

void arch_init() TA_NO_THREAD_SAFETY_ANALYSIS {
    arch_mp_init_percpu();

    dprintf(INFO, "ARM boot EL%lu\n", arm64_get_boot_el());

    arm64_feature_debug(true);

    uint32_t max_cpus = arch_max_num_cpus();
    uint32_t cmdline_max_cpus = cmdline_get_uint32("kernel.smp.maxcpus", max_cpus);
    if (cmdline_max_cpus > max_cpus || cmdline_max_cpus <= 0) {
        printf("invalid kernel.smp.maxcpus value, defaulting to %u\n", max_cpus);
        cmdline_max_cpus = max_cpus;
    }

    secondaries_to_init = cmdline_max_cpus - 1;

    lk_init_secondary_cpus(secondaries_to_init);

    LTRACEF("releasing %d secondary cpus\n", secondaries_to_init);

    // Release the secondary cpus.
    spin_unlock(&arm_boot_cpu_lock);

    // Flush the release of the lock, since the secondary cpus are running without cache on.
    arch_clean_cache_range((addr_t)&arm_boot_cpu_lock, sizeof(arm_boot_cpu_lock));
}

__NO_RETURN int arch_idle_thread_routine(void*) {
    for (;;)
        __asm__ volatile("wfi");
}

// Switch to user mode, set the user stack pointer to user_stack_top, put the svc stack pointer to
// the top of the kernel stack.
void arch_enter_uspace(uintptr_t pc, uintptr_t sp, uintptr_t arg1, uintptr_t arg2) {
    thread_t* ct = get_current_thread();

    // Set up a default spsr to get into 64bit user space:
    //  - Zeroed NZCV.
    //  - No SS, no IL, no D.
    //  - All interrupts enabled.
    //  - Mode 0: EL0t.
    //
    // TODO: (hollande,travisg) Need to determine why some platforms throw an
    //         SError exception when first switching to uspace.
    uint32_t spsr = 1 << 8;  // Mask SError exceptions (currently unhandled).

    arch_disable_ints();

    LTRACEF("arm_uspace_entry(%#" PRIxPTR ", %#" PRIxPTR ", %#x, %#" PRIxPTR
            ", %#" PRIxPTR ", 0, %#" PRIxPTR ")\n",
            arg1, arg2, spsr, ct->stack_top, sp, pc);
    arm64_uspace_entry(arg1, arg2, pc, sp, ct->stack_top, spsr, MSDCR_EL1_INITIAL_VALUE);
    __UNREACHABLE;
}

// called from assembly.
extern "C" void arm64_secondary_entry() {
    arm64_cpu_early_init();

    spin_lock(&arm_boot_cpu_lock);
    spin_unlock(&arm_boot_cpu_lock);

    uint cpu = arch_curr_cpu_num();
    thread_secondary_cpu_init_early(&_init_thread[cpu - 1]);
    // Run early secondary cpu init routines up to the threading level.
    lk_init_level(LK_INIT_FLAG_SECONDARY_CPUS, LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_THREADING - 1);

    arch_mp_init_percpu();

    arm64_feature_debug(false);

    lk_secondary_cpu_entry();
}
