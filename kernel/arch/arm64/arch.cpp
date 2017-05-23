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
#include <magenta/errors.h>
#include <inttypes.h>
#include <platform.h>
#include <trace.h>

#define LOCAL_TRACE 0

enum {
    PMCR_EL0_ENABLE_BIT         = 1 << 0,
    PMCR_EL0_LONG_COUNTER_BIT   = 1 << 6,
};


typedef struct {
    uint64_t    mpid;
    void*       sp;

    // This part of the struct itself will serve temporarily as the
    // fake arch_thread in the thread pointer, so that safe-stack
    // and stack-protector code can work early.  The thread pointer
    // (TPIDR_EL1) points just past arm64_sp_info_t.
    uintptr_t   stack_guard;
    void*       unsafe_sp;
} arm64_sp_info_t;

static_assert(sizeof(arm64_sp_info_t) == 32,
              "check arm64_get_secondary_sp assembly");
static_assert(offsetof(arm64_sp_info_t, sp) == 8,
              "check arm64_get_secondary_sp assembly");
static_assert(offsetof(arm64_sp_info_t, mpid) == 0,
              "check arm64_get_secondary_sp assembly");

#define TP_OFFSET(field) \
    ((int)offsetof(arm64_sp_info_t, field) - (int)sizeof(arm64_sp_info_t))
static_assert(TP_OFFSET(stack_guard) == MX_TLS_STACK_GUARD_OFFSET, "");
static_assert(TP_OFFSET(unsafe_sp) == MX_TLS_UNSAFE_SP_OFFSET, "");
#undef TP_OFFSET

#if WITH_SMP
/* smp boot lock */
static spin_lock_t arm_boot_cpu_lock = 1;
static volatile int secondaries_to_init = 0;
static thread_t _init_thread[SMP_MAX_CPUS - 1];
arm64_sp_info_t arm64_secondary_sp_list[SMP_MAX_CPUS];
#endif

static arm64_cache_info_t cache_info[SMP_MAX_CPUS];

extern uint64_t arch_boot_el; // Defined in start.S.

uint64_t arm64_get_boot_el(void)
{
    return arch_boot_el >> 2;
}

#if WITH_SMP
status_t arm64_set_secondary_sp(uint cluster, uint cpu,
                                void* sp, void* unsafe_sp) {
    uint64_t mpid = ARM64_MPID(cluster, cpu);

    uint32_t i = 0;
    while ((i < SMP_MAX_CPUS) && (arm64_secondary_sp_list[i].mpid != 0)) {
        i++;
    }
    if (i==SMP_MAX_CPUS)
        return ERR_NO_RESOURCES;
    printf("Set mpid 0x%lx sp to %p\n", mpid, sp);
#if __has_feature(safe_stack)
    printf("Set mpid 0x%lx unsafe-sp to %p\n", mpid, unsafe_sp);
#else
    DEBUG_ASSERT(unsafe_sp == NULL);
#endif
    arm64_secondary_sp_list[i].mpid = mpid;
    arm64_secondary_sp_list[i].sp = sp;
    arm64_secondary_sp_list[i].stack_guard = get_current_thread()->arch.stack_guard;
    arm64_secondary_sp_list[i].unsafe_sp = unsafe_sp;

    return NO_ERROR;
}
#endif

static void parse_ccsid(arm64_cache_desc_t* desc, uint64_t ccsid) {
    desc->write_through = BIT(ccsid, 31) > 0;
    desc->write_back    = BIT(ccsid, 30) > 0;
    desc->read_alloc    = BIT(ccsid, 29) > 0;
    desc->write_alloc   = BIT(ccsid, 28) > 0;
    desc->num_sets      = (uint32_t)BITS_SHIFT(ccsid, 27, 13) + 1;
    desc->associativity = (uint32_t)BITS_SHIFT(ccsid, 12, 3)  + 1;
    desc->line_size     = 1u << (BITS(ccsid, 2, 0) + 4);
}

void arm64_get_cache_info(arm64_cache_info_t* info) {
    uint64_t temp=0;

    uint64_t sysreg = ARM64_READ_SYSREG(clidr_el1);
    info->inner_boundary    = (uint8_t)BITS_SHIFT(sysreg, 32, 30);
    info->lou_u             = (uint8_t)BITS_SHIFT(sysreg, 29, 27);
    info->loc               = (uint8_t)BITS_SHIFT(sysreg, 26, 24);
    info->lou_is            = (uint8_t)BITS_SHIFT(sysreg, 23, 21);
    for (int i = 0; i < 7; i++) {
        uint8_t ctype = (sysreg >> (3*i)) & 0x07;
        if (ctype == 0) {
            info->level_data_type[i].ctype = 0;
            info->level_inst_type[i].ctype = 0;
        } else if (ctype == 4) {                                // Unified
            ARM64_WRITE_SYSREG(CSSELR_EL1, (int64_t)(i << 1));  // Select cache level
            temp = ARM64_READ_SYSREG(ccsidr_el1);
            info->level_data_type[i].ctype = 4;
            parse_ccsid(&(info->level_data_type[i]),temp);
        } else {
            if (ctype & 0x02) {
                ARM64_WRITE_SYSREG(CSSELR_EL1, (int64_t)(i << 1));
                temp = ARM64_READ_SYSREG(ccsidr_el1);
                info->level_data_type[i].ctype = 2;
                parse_ccsid(&(info->level_data_type[i]),temp);
            }
            if (ctype & 0x01) {
                ARM64_WRITE_SYSREG(CSSELR_EL1, (int64_t)(i << 1) | 0x01);
                temp = ARM64_READ_SYSREG(ccsidr_el1);
                info->level_inst_type[i].ctype = 1;
                parse_ccsid(&(info->level_inst_type[i]),temp);
            }
        }
    }
}

void arm64_dump_cache_info(uint32_t cpu) {

    arm64_cache_info_t*  info = &(cache_info[cpu]);
    printf("==== ARM64 CACHE INFO CORE %u====\n",cpu);
    printf("Inner Boundary = L%u\n",info->inner_boundary);
    printf("Level of Unification Uinprocessor = L%u\n", info->lou_u);
    printf("Level of Coherence = L%u\n", info->loc);
    printf("Level of Unification Inner Shareable = L%u\n",info->lou_is);
    for (int i = 0; i < 7; i++) {
        printf("L%d Details:\n",i+1);
        if ((info->level_data_type[i].ctype == 0) && (info->level_inst_type[i].ctype == 0)) {
            printf("\tNot Implemented\n");
        } else {
            if (info->level_data_type[i].ctype == 4) {
                printf("\tUnified Cache, sets=%u, associativity=%u, line size=%u bytes\n", info->level_data_type[i].num_sets,
                                                                    info->level_data_type[i].associativity,
                                                                    info->level_data_type[i].line_size);
            } else {
                if (info->level_data_type[i].ctype & 0x02) {
                    printf("\tData Cache, sets=%u, associativity=%u, line size=%u bytes\n", info->level_data_type[i].num_sets,
                                                                    info->level_data_type[i].associativity,
                                                                    info->level_data_type[i].line_size);
                }
                if (info->level_inst_type[i].ctype & 0x01) {
                    printf("\tInstruction Cache, sets=%u, associativity=%u, line size=%u bytes\n", info->level_inst_type[i].num_sets,
                                                                    info->level_inst_type[i].associativity,
                                                                    info->level_inst_type[i].line_size);
                }
            }
        }
    }
}

static void arm64_cpu_early_init(void)
{
    uint64_t mmfr0 = ARM64_READ_SYSREG(ID_AA64MMFR0_EL1);

    /* check to make sure implementation supports 16 bit asids */
    ASSERT( (mmfr0 & ARM64_MMFR0_ASIDBITS_MASK) == ARM64_MMFR0_ASIDBITS_16);

    /* set the vector base */
    ARM64_WRITE_SYSREG(VBAR_EL1, (uint64_t)&arm64_exception_base);

    /* switch to EL1 */
    uint64_t current_el = ARM64_READ_SYSREG(CURRENTEL) >> 2;
    if (current_el > 1) {
        arm64_el3_to_el1();
    }

    /* set some control bits in sctlr */
    uint64_t sctlr = ARM64_READ_SYSREG(sctlr_el1);
    sctlr |= (1<<26);  /* UCI - Allow certain cache ops in EL0 */
    sctlr |= (1<<15);  /* UCT - Allow EL0 access to CTR register */
    sctlr |= (1<<14);  /* DZE - Allow EL0 to use DC ZVA */
    sctlr |= (1<<4);   /* SA0 - Enable Stack Alignment Check EL0 */
    sctlr |= (1<<3);   /* SA  - Enable Stack Alignment Check EL1 */
    sctlr &= ~(1<<1);  /* AC  - Disable Alignment Checking for EL1 EL0 */
    ARM64_WRITE_SYSREG(sctlr_el1, sctlr);

    arch_enable_fiqs();

    /* enable cycle counter */
    ARM64_WRITE_SYSREG(pmcr_el0, (uint64_t)(PMCR_EL0_ENABLE_BIT | PMCR_EL0_LONG_COUNTER_BIT));
    ARM64_WRITE_SYSREG(pmcntenset_el0, (1UL << 31));

    /* enable user space access to cycle counter */
    ARM64_WRITE_SYSREG(pmuserenr_el0, 1UL);

    /* enable user space access to virtual counter (CNTVCT_EL0) */
    ARM64_WRITE_SYSREG(cntkctl_el1, 1UL << 1);

    uint32_t cpu = arch_curr_cpu_num();
    arm64_get_cache_info(&(cache_info[cpu]));
}

void arch_early_init(void)
{
    arm64_cpu_early_init();

    /* read the block size of DC ZVA */
    uint64_t dczid = ARM64_READ_SYSREG(dczid_el0);
    if (BIT(dczid, 4) == 0) {
        arm64_zva_shift = (uint32_t)(ARM64_READ_SYSREG(dczid_el0) & 0xf) + 2;
    }
    ASSERT(arm64_zva_shift != 0); /* for now, fail if DC ZVA is unavailable */

    platform_init_mmu_mappings();
}

void arch_init(void)
{
#if WITH_SMP
    arch_mp_init_percpu();

    LTRACEF("midr_el1 %#" PRIx64 "\n", ARM64_READ_SYSREG(midr_el1));

    uint32_t max_cpus = arch_max_num_cpus();
    uint32_t cmdline_max_cpus = cmdline_get_uint32("smp.maxcpus", max_cpus);
    if (cmdline_max_cpus > max_cpus || cmdline_max_cpus <= 0) {
        printf("invalid smp.maxcpus value, defaulting to %u\n", max_cpus);
        cmdline_max_cpus = max_cpus;
    }

    secondaries_to_init = cmdline_max_cpus - 1;

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
    // TODO: (hollande,travisg) Need to determine why some platforms throw an
    //         SError exception when first switching to uspace.
    uint32_t spsr = (uint32_t)(1 << 8); //Mask SError exceptions (currently unhandled)

    arch_disable_ints();

    LTRACEF("arm_uspace_entry(%#" PRIxPTR ", %#" PRIxPTR ", %#x, %#" PRIxPTR
            ", %#" PRIxPTR ", 0, %#" PRIxPTR ")\n",
            arg1, arg2, spsr, ct->stack_top, sp, pc);
    arm64_uspace_entry(arg1, arg2, pc, sp, ct->stack_top, spsr);
    __UNREACHABLE;
}

#if WITH_SMP
/* called from assembly */
extern "C" void arm64_secondary_entry(void)
{
    uint cpu = arch_curr_cpu_num();

    arm64_cpu_early_init();

    spin_lock(&arm_boot_cpu_lock);
    spin_unlock(&arm_boot_cpu_lock);

    thread_secondary_cpu_init_early(&_init_thread[cpu - 1]);
    /* run early secondary cpu init routines up to the threading level */
    lk_init_level(LK_INIT_FLAG_SECONDARY_CPUS, LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_THREADING - 1);

    arch_mp_init_percpu();

    LTRACEF("cpu num %u\n", cpu);

    lk_secondary_cpu_entry();
}
#endif
