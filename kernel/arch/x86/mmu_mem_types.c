// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <arch/x86.h>
#include <arch/x86/mmu.h>
#include <arch/x86/registers.h>
#include <lib/console.h>
#include <kernel/mp.h>

/* address widths from mmu.c */
extern uint8_t g_paddr_width;

/* MTRR MSRs */
#define IA32_MTRRCAP 0xfe
#define IA32_MTRR_DEF_TYPE 0x2ff
#define IA32_MTRR_FIX64K_00000 0x250
#define IA32_MTRR_NUM_FIX16K 2
#define IA32_MTRR_FIX16K_80000(x) (0x258 + (x))
#define IA32_MTRR_NUM_FIX4K 8
#define IA32_MTRR_FIX4K_C0000(x) (0x268 + (x))
#define IA32_MTRR_PHYSBASE(x) (0x200 + 2 * (x))
#define IA32_MTRR_PHYSMASK(x) (0x201 + 2 * (x))

/* PAT MSRs */
#define IA32_PAT 0x277

/* IA32_MTRRCAP read functions */
#define MTRRCAP_VCNT(x) ((x) & 0xff)
#define MTRRCAP_FIX(x) !!((x) & (1 << 8))
#define MTRRCAP_WC(x) !!((x) & (1 << 10))

/* IA32_MTRR_DEF_TYPE read functions */
/* global enable flag for MTRRs */
#define MTRR_DEF_TYPE_ENABLE(x) ((x) & (1 << 11))
/* enable flag for fixed-range MTRRs */
#define MTRR_DEF_TYPE_FIXED_ENABLE(x) ((x) & (1 << 10))
#define MTRR_DEF_TYPE_TYPE(x) ((uint8_t)(x))

/* IA32_MTRR_DEF_TYPE masks */
#define MTRR_DEF_TYPE_ENABLE_FLAG (1 << 11)
#define MTRR_DEF_TYPE_FIXED_ENABLE_FLAG (1 << 10)
#define MTRR_DEF_TYPE_TYPE_MASK 0xff

/* IA32_MTRR_PHYSBASE read functions */
#define MTRR_PHYSBASE_BASE(x) ((x) & ~((1ULL << 12) - 1) & ((1ULL << g_paddr_width) - 1))
#define MTRR_PHYSBASE_TYPE(x) ((uint8_t)(x))

/* IA32_MTRR_PHYSMASK read functions */
#define MTRR_PHYSMASK_MASK(x) ((x) & ~((1ULL << 12) - 1) & ((1ULL << g_paddr_width) - 1))
#define MTRR_PHYSMASK_VALID(x) !!((x) & (1 << 11))

/* Number of variable length MTRRs */
static uint8_t num_variable = 0;
/* Whether or not fixed range MTRRs are supported */
static bool supports_fixed_range = false;
/* Whether write-combining memory type is supported */
static bool supports_wc = false;

struct variable_mtrr {
    uint64_t physbase;
    uint64_t physmask;
};

struct mtrrs {
    uint64_t mtrr_def;
    uint64_t mtrr_fix64k;
    uint64_t mtrr_fix16k[IA32_MTRR_NUM_FIX16K];
    uint64_t mtrr_fix4k[IA32_MTRR_NUM_FIX4K];
    struct variable_mtrr mtrr_var[];
};
static struct mtrrs *target_mtrrs;

/* Function called by all CPUs to setup their PAT */
static void x86_pat_sync_task(void *context);
struct pat_sync_task_context {
    /* Barrier counters for the two barriers described in Intel's algorithm */
    volatile int barrier1;
    volatile int barrier2;
};

extern void* boot_alloc_mem(size_t len);
void x86_mmu_mem_type_init(void)
{
    uint64_t caps = read_msr(IA32_MTRRCAP);
    num_variable = MTRRCAP_VCNT(caps);
    supports_fixed_range = MTRRCAP_FIX(caps);
    supports_wc = MTRRCAP_WC(caps);

    target_mtrrs = boot_alloc_mem(
            sizeof(struct mtrrs) + sizeof(struct variable_mtrr) * num_variable);
    ASSERT(target_mtrrs);

    target_mtrrs->mtrr_def = read_msr(IA32_MTRR_DEF_TYPE);
    target_mtrrs->mtrr_fix64k = read_msr(IA32_MTRR_FIX64K_00000);
    for (uint i = 0; i < IA32_MTRR_NUM_FIX16K; ++i) {
        target_mtrrs->mtrr_fix16k[i] = read_msr(IA32_MTRR_FIX16K_80000(i));
    }
    for (uint i = 0; i < IA32_MTRR_NUM_FIX4K; ++i) {
        target_mtrrs->mtrr_fix4k[i] = read_msr(IA32_MTRR_FIX4K_C0000(i));
    }
    for (uint i = 0; i < num_variable; ++i) {
        target_mtrrs->mtrr_var[i].physbase = read_msr(IA32_MTRR_PHYSBASE(i));
        target_mtrrs->mtrr_var[i].physmask = read_msr(IA32_MTRR_PHYSMASK(i));
    }

    /* Update the PAT on the bootstrap processor (and sync any changes to the
     * MTRR that may have been made above). */
    x86_pat_sync();
}

/* @brief Give all CPUs our Page Attribute Tables and Memory Type Range
 * Registers
 *
 * This operation is not safe to perform while a CPU may be
 * hotplugged.
 *
 * This algorithm is based on section 11.11.8 of Intel 3A
 * This must only be called after the APs are brought up.
 */
void x86_pat_sync(void)
{
    mp_cpu_mask_t online = mp_get_online_mask();

    struct pat_sync_task_context context = {
        .barrier1 = online,
        .barrier2 = online,
    };
    /* Step 1: Broadcast to all processors to execute the sequence */
    mp_sync_exec(online, x86_pat_sync_task, &context);
}

static void x86_pat_sync_task(void *raw_context)
{
    /* Step 2: Disable interrupts */
    DEBUG_ASSERT(arch_ints_disabled());

    struct pat_sync_task_context *context = raw_context;

    uint cpu = arch_curr_cpu_num();

    /* Step 3: Wait for all processors to reach this point. */
    atomic_and(&context->barrier1, ~(1 << cpu));
    while (context->barrier1 != 0) {
        arch_spinloop_pause();
    }

    /* Step 4: Enter the no-fill cache mode (cache-disable and writethrough) */
    ulong cr0 = x86_get_cr0();
    DEBUG_ASSERT(!(cr0 & X86_CR0_CD) && !(cr0 & X86_CR0_NW));
    cr0 |= X86_CR0_CD;
    cr0 &= ~X86_CR0_NW;
    x86_set_cr0(cr0);

    /* Step 5: Flush all caches */
    __asm volatile ("wbinvd" ::: "memory");

    /* Step 6: If the PGE flag is set, clear it to flush the TLB */
    ulong cr4 = x86_get_cr4();
    bool pge_was_set = !!(cr4 & X86_CR4_PGE);
    cr4 &= ~X86_CR4_PGE;
    x86_set_cr4(cr4);

    /* Step 7: If the PGE flag wasn't set, flush the TLB via CR3 */
    if (!pge_was_set) {
        x86_set_cr3(x86_get_cr3());
    }

    /* Step 8: Disable MTRRs */
    write_msr(IA32_MTRR_DEF_TYPE, 0);

    /* Step 9: Sync up the MTRR entries */
    write_msr(IA32_MTRR_FIX64K_00000, target_mtrrs->mtrr_fix64k);
    for (uint i = 0; i < IA32_MTRR_NUM_FIX16K; ++i) {
        write_msr(IA32_MTRR_FIX16K_80000(i), target_mtrrs->mtrr_fix16k[i]);
    }
    for (uint i = 0; i < IA32_MTRR_NUM_FIX4K; ++i) {
        write_msr(IA32_MTRR_FIX4K_C0000(i), target_mtrrs->mtrr_fix4k[i]);
    }
    for (uint i = 0; i < num_variable; ++i) {
        write_msr(IA32_MTRR_PHYSBASE(i), target_mtrrs->mtrr_var[i].physbase);
        write_msr(IA32_MTRR_PHYSMASK(i), target_mtrrs->mtrr_var[i].physmask);
    }

    /* For now, we leave the MTRRs as the firmware gave them to us, except for
     * setting the default memory type to uncached */

    /* Starting from here, we diverge from the algorithm in 11.11.8.  That
     * algorithm is for MTRR changes, and 11.12.4 suggests using a variant of
     * it.
     */

    /* Perform PAT changes now that caches aren't being filled and the
     * TLB is flushed. */
    uint64_t pat_val = 0;
    pat_val |= (uint64_t)X86_PAT_INDEX0 << 0;
    pat_val |= (uint64_t)X86_PAT_INDEX1 << 8;
    pat_val |= (uint64_t)X86_PAT_INDEX2 << 16;
    pat_val |= (uint64_t)X86_PAT_INDEX3 << 24;
    pat_val |= (uint64_t)X86_PAT_INDEX4 << 32;
    pat_val |= (uint64_t)X86_PAT_INDEX5 << 40;
    pat_val |= (uint64_t)X86_PAT_INDEX6 << 48;
    pat_val |= (uint64_t)X86_PAT_INDEX7 << 56;
    write_msr(IA32_PAT, pat_val);

    /* Step 10: Re-enable MTRRs (and set the default type) */
    write_msr(IA32_MTRR_DEF_TYPE, target_mtrrs->mtrr_def);

    /* Step 11: Flush all cache and the TLB again */
    __asm volatile ("wbinvd" ::: "memory");
    x86_set_cr3(x86_get_cr3());

    /* Step 12: Enter the normal cache mode */
    cr0 = x86_get_cr0();
    cr0 &= ~(X86_CR0_CD | X86_CR0_NW);
    x86_set_cr0(cr0);

    /* Step 13: Re-enable PGE if it was previously set */
    if (pge_was_set) {
        cr4 = x86_get_cr4();
        cr4 |= X86_CR4_PGE;
        x86_set_cr4(cr4);
    }

    /* Step 14: Wait for all processors to reach this point. */
    atomic_and(&context->barrier2, ~(1 << cpu));
    while (context->barrier2 != 0) {
        arch_spinloop_pause();
    }
}

/* Helper for decoding and printing MTRRs */
static void print_fixed_range_mtrr(uint32_t msr, uint32_t base, uint32_t record_size)
{
    uint64_t val = read_msr(msr);
    for (int i = 0; i < 8; ++i) {
        printf("  f %#05x-%#05x: %#02x\n", base, base + record_size - 1, (uint8_t)val);
        base += record_size;
        val >>= 8;
    }
}

static void print_pat_entries(void *_ignored)
{
    uint64_t pat = read_msr(IA32_PAT);
    for (int i = 0; i < 8; ++i) {
        printf("  Index %d: %#02x\n", i, (uint8_t)pat);
        pat >>= 8;
    }
}

static int cmd_memtype(int argc, const cmd_args *argv)
{
    if (argc < 2) {
notenoughargs:
        printf("not enough arguments\n");
usage:
        printf("usage:\n");
        printf("%s mtrr\n", argv[0].str);
        printf("%s pat\n", argv[0].str);
        return ERR_GENERIC;
    }

    if (!strcmp(argv[1].str, "mtrr")) {
        bool print_fixed = false;
        if (argc > 2) {
            if (!strcmp(argv[2].str, "-f")) {
                print_fixed = true;
            } else {
                printf("usage: %s mtrr [-f]\n", argv[0].str);
                printf("  -f    Display fixed registers\n");
                return ERR_GENERIC;
            }
        }
        uint64_t default_type = read_msr(IA32_MTRR_DEF_TYPE);
        printf("MTRR state: master %s, fixed %s\n",
               MTRR_DEF_TYPE_ENABLE(default_type) ? "enable" : "disable",
               MTRR_DEF_TYPE_FIXED_ENABLE(default_type) ? "enable" : "disable");
        printf("  default: %#02x\n", MTRR_DEF_TYPE_TYPE(default_type));
        if (supports_fixed_range && print_fixed) {
            print_fixed_range_mtrr(IA32_MTRR_FIX64K_00000, 0x00000, (1 << 16));
            for (int i = 0; i < IA32_MTRR_NUM_FIX16K; ++i) {
                print_fixed_range_mtrr(IA32_MTRR_FIX16K_80000(i), 0x80000 + i * (1<<17), (1 << 14));
            }
            for (int i = 0; i < IA32_MTRR_NUM_FIX4K; ++i) {
                print_fixed_range_mtrr(IA32_MTRR_FIX4K_C0000(i), 0xC0000 + i * (1<<15), (1 << 12));
            }
        }

        for (uint i = 0; i < num_variable; ++i) {
            uint64_t base = read_msr(IA32_MTRR_PHYSBASE(i));
            uint64_t mask = read_msr(IA32_MTRR_PHYSMASK(i));
            printf("  v (%s) base %#016llx, mask %#016llx: %#02x\n",
                   MTRR_PHYSMASK_VALID(mask) ? "valid" : "invalid",
                   MTRR_PHYSBASE_BASE(base),
                   MTRR_PHYSMASK_MASK(mask),
                   MTRR_PHYSBASE_TYPE(base));
        }
    } else if (!strcmp(argv[1].str, "pat")) {
        uint num_cpus = arch_max_num_cpus();
        for (uint i = 0; i < num_cpus; ++i) {
            printf("CPU %u Page Attribute Table types:\n", i);
            mp_sync_exec(1 << i, print_pat_entries, NULL);
        }
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return NO_ERROR;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("memtype", "memory type commands", &cmd_memtype)
#endif
STATIC_COMMAND_END(memtype);
