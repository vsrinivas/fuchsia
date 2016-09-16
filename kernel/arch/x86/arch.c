// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <assert.h>
#include <magenta/compiler.h>
#include <debug.h>
#include <err.h>
#include <trace.h>
#include <assert.h>
#include <inttypes.h>
#include <arch.h>
#include <arch/ops.h>
#include <arch/mp.h>
#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mmu_mem_types.h>
#include <arch/x86/mp.h>
#include <arch/mmu.h>
#include <kernel/vm.h>
#include <lib/console.h>
#include <lk/init.h>
#include <lk/main.h>
#include <platform.h>
#include <sys/types.h>
#include <string.h>

#define LOCAL_TRACE 0

/* early stack */
uint8_t _kstack[PAGE_SIZE] __ALIGNED(16);

/* save a pointer to the multiboot information coming in from whoever called us */
/* make sure it lives in .data to avoid it being wiped out by bss clearing */
__SECTION(".data") void *_multiboot_info;

/* also save a pointer to the boot_params structure */
__SECTION(".data") void *_zero_page_boot_params;

void arch_early_init(void)
{
    x86_mmu_percpu_init();
    x86_mmu_early_init();
}

void arch_init(void)
{
    x86_feature_debug();

    x86_mmu_init();
}

void arch_chain_load(void *entry, ulong arg0, ulong arg1, ulong arg2, ulong arg3)
{
    PANIC_UNIMPLEMENTED;
}

void arch_enter_uspace(uintptr_t entry_point, uintptr_t sp,
                       uintptr_t arg1, uintptr_t arg2) {
    LTRACEF("entry %#" PRIxPTR " user stack %#" PRIxPTR "\n", entry_point, sp);

    arch_disable_ints();

#if ARCH_X86_32
    PANIC_UNIMPLEMENTED;
#elif ARCH_X86_64
    /* default user space flags:
     * IOPL 0
     * Interrupts enabled
     */
    ulong flags = (0 << X86_FLAGS_IOPL_SHIFT) | X86_FLAGS_IF;

    /* check that we're probably still pointed at the kernel gs */
    DEBUG_ASSERT(is_kernel_address(read_msr(X86_MSR_IA32_GS_BASE)));

    /* set up user's fs: gs: base */
    write_msr(X86_MSR_IA32_FS_BASE, 0);

    /* set the KERNEL_GS_BASE msr here, because we're going to swapgs below */
    write_msr(X86_MSR_IA32_KERNEL_GS_BASE, 0);

    x86_uspace_entry(arg1, arg2, sp, entry_point, flags);
    __UNREACHABLE;
#endif
}

#if WITH_SMP
#include <arch/x86/apic.h>
void x86_secondary_entry(volatile int *aps_still_booting, thread_t *thread)
{
    // Would prefer this to be in init_percpu, but there is a dependency on a
    // page mapping existing, and the BP calls that before the VM subsystem is
    // initialized.
    apic_local_init();

    uint32_t local_apic_id = apic_local_id();
    int cpu_num = x86_apic_id_to_cpu_num(local_apic_id);
    if (cpu_num < 0) {
        // If we could not find our CPU number, do not proceed further
        arch_disable_ints();
        while (1) {
            x86_hlt();
        }
    }

    DEBUG_ASSERT(cpu_num > 0);
    x86_init_percpu((uint)cpu_num);

    // Signal that this CPU is initialized.  It is important that after this
    // operation, we do not touch any resources associated with bootstrap
    // besides our thread_t and stack, since this is the checkpoint the
    // bootstrap process uses to identify completion.
    int old_val = atomic_and(aps_still_booting, ~(1U << cpu_num));
    if (old_val == 0) {
        // If the value is already zero, then booting this CPU timed out.
        goto fail;
    }

    // Defer configuring memory settings until after the atomic_and above.
    // This ensures that we were in no-fill cache mode for the duration of early
    // AP init.
    DEBUG_ASSERT(x86_get_cr0() & X86_CR0_CD);
    x86_mmu_percpu_init();

    // Load the appropriate PAT/MTRRs.  This must happen after init_percpu, so
    // that this CPU is considered online.
    x86_pat_sync(1U << cpu_num);

    /* run early secondary cpu init routines up to the threading level */
    lk_init_level(LK_INIT_FLAG_SECONDARY_CPUS, LK_INIT_LEVEL_EARLIEST, LK_INIT_LEVEL_THREADING - 1);

    thread_secondary_cpu_init_early(thread);
    /* The thread stack and struct are from a single allocation, free it when we
     * exit into the scheduler */
    thread->flags |= THREAD_FLAG_FREE_STRUCT;
    lk_secondary_cpu_entry();

    // lk_secondary_cpu_entry only returns on an error, halt the core in this
    // case.
fail:
    arch_disable_ints();
    while (1) {
      x86_hlt();
    }
}
#endif

static int cmd_cpu(int argc, const cmd_args *argv)
{
    if (argc < 2) {
notenoughargs:
        printf("not enough arguments\n");
usage:
        printf("usage:\n");
        printf("%s features\n", argv[0].str);
        printf("%s unplug <cpu_id>\n", argv[0].str);
        printf("%s hotplug <cpu_id>\n", argv[0].str);
        return ERR_INTERNAL;
    }

    if (!strcmp(argv[1].str, "features")) {
        x86_feature_debug();
    } else if (!strcmp(argv[1].str, "unplug")) {
        if (argc < 3) {
            printf("specify a cpu_id\n");
            goto usage;
        }
        status_t status = ERR_NOT_SUPPORTED;
#if WITH_SMP
        status = mp_unplug_cpu(argv[2].u);
#endif
        printf("CPU %lu unplugged: %d\n", argv[2].u, status);
    } else if (!strcmp(argv[1].str, "hotplug")) {
        if (argc < 3) {
            printf("specify a cpu_id\n");
            goto usage;
        }
        status_t status = ERR_NOT_SUPPORTED;
#if WITH_SMP
        status = mp_hotplug_cpu(argv[2].u);
#endif
        printf("CPU %lu hotplugged: %d\n", argv[2].u, status);
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return NO_ERROR;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("cpu", "cpu test commands", &cmd_cpu)
#endif
STATIC_COMMAND_END(cpu);
