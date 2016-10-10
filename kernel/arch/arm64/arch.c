// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014-2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <stdlib.h>
#include <arch.h>
#include <arch/ops.h>
#include <arch/arm64.h>
#include <arch/arm64/mmu.h>
#include <arch/mp.h>
#include <bits.h>
#include <kernel/cmdline.h>
#include <kernel/thread.h>
#include <lk/init.h>
#include <lk/main.h>
#include <inttypes.h>
#include <platform.h>
#include <trace.h>

#define LOCAL_TRACE 0

#if WITH_SMP
/* smp boot lock */
static spin_lock_t arm_boot_cpu_lock = 1;
static volatile int secondaries_to_init = 0;
uint arm_num_cpus = 1;
static thread_t _init_thread[SMP_MAX_CPUS - 1];
#endif

static void arm64_cpu_early_init(void)
{
    uint64_t mmfr0 = ARM64_READ_SYSREG(ID_AA64MMFR0_EL1);

    /* check to make sure implementation supports 16 bit asids */
    ASSERT( (mmfr0 & ARM64_MMFR0_ASIDBITS_MASK) == ARM64_MMFR0_ASIDBITS_16);

    /* set the vector base */
    ARM64_WRITE_SYSREG(VBAR_EL1, (uint64_t)&arm64_exception_base);

    /* switch to EL1 */
    unsigned int current_el = ARM64_READ_SYSREG(CURRENTEL) >> 2;
    if (current_el > 1) {
        arm64_el3_to_el1();
    }

    arch_enable_fiqs();

    /* enable cycle counter */
    ARM64_WRITE_SYSREG(pmcr_el0, 1);
    ARM64_WRITE_SYSREG(pmcntenset_el0, (1u << 31));
}

void arch_early_init(void)
{
    arm64_cpu_early_init();

    /* read the block size of DC ZVA */
    uint32_t dczid = ARM64_READ_SYSREG(dczid_el0);
    if (BIT(dczid, 4) == 0) {
        arm64_zva_shift = (ARM64_READ_SYSREG(dczid_el0) & 0xf) + 2;
    }
    ASSERT(arm64_zva_shift != 0); /* for now, fail if DC ZVA is unavailable */

    platform_init_mmu_mappings();
}

void arch_init(void)
{
#if WITH_SMP
    arch_mp_init_percpu();

    LTRACEF("midr_el1 %#" PRIx64 "\n", ARM64_READ_SYSREG(midr_el1));

    uint32_t cmdline_max_cpus = cmdline_get_uint32("smp.maxcpus", SMP_MAX_CPUS);
    if (cmdline_max_cpus > SMP_MAX_CPUS || cmdline_max_cpus <= 0) {
        printf("invalid smp.maxcpus value, defaulting to %d\n", SMP_MAX_CPUS);
        cmdline_max_cpus = SMP_MAX_CPUS;
    }

    secondaries_to_init = cmdline_max_cpus - 1; /* TODO: get count from somewhere else, or add cpus as they boot */

    lk_init_secondary_cpus(secondaries_to_init);

    LTRACEF("releasing %d secondary cpus\n", secondaries_to_init);

    /* release the secondary cpus */
    spin_unlock(&arm_boot_cpu_lock);

    /* flush the release of the lock, since the secondary cpus are running without cache on */
    arch_clean_cache_range((addr_t)&arm_boot_cpu_lock, sizeof(arm_boot_cpu_lock));
#endif
}

void arch_quiesce(void)
{
}

void arch_idle(void)
{
    __asm__ volatile("wfi");
}

void arch_chain_load(void *entry, ulong arg0, ulong arg1, ulong arg2, ulong arg3)
{
    PANIC_UNIMPLEMENTED;
}

/* switch to user mode, set the user stack pointer to user_stack_top, put the svc stack pointer to the top of the kernel stack */
void arch_enter_uspace(uintptr_t pc, uintptr_t sp, uintptr_t arg1, uintptr_t arg2) {
    thread_t *ct = get_current_thread();

    /* set up a default spsr to get into 64bit user space:
     * zeroed NZCV
     * no SS, no IL, no D
     * all interrupts enabled
     * mode 0: EL0t
     */
    uint32_t spsr = 0;

    arch_disable_ints();

    LTRACEF("arm_uspace_entry(%#" PRIxPTR ", %#" PRIxPTR ", %#x, %#" PRIxPTR
            ", %#" PRIxPTR ", 0, %#" PRIxPTR ")\n",
            arg1, arg2, spsr, ct->stack_top, sp, pc);
    arm64_uspace_entry(arg1, arg2, pc, sp, ct->stack_top, spsr);
    __UNREACHABLE;
}

#if WITH_SMP
/* called from assembly */
void arm64_secondary_entry(ulong asm_cpu_num);

void arm64_secondary_entry(ulong asm_cpu_num)
{
    uint cpu = arch_curr_cpu_num();
    if (cpu != asm_cpu_num)
        return;

    arm64_cpu_early_init();

    spin_lock(&arm_boot_cpu_lock);
    spin_unlock(&arm_boot_cpu_lock);

    /* run early secondary cpu init routines up to the threading level */
    lk_init_level(LK_INIT_FLAG_SECONDARY_CPUS, LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_THREADING - 1);

    arch_mp_init_percpu();

    LTRACEF("cpu num %u\n", cpu);

    /* we're done, tell the main cpu we're up */
    atomic_add(&secondaries_to_init, -1);
    atomic_add((int *)&arm_num_cpus, 1);
    __asm__ volatile("sev");

    thread_secondary_cpu_init_early(&_init_thread[cpu - 1]);
    lk_secondary_cpu_entry();
}
#endif
