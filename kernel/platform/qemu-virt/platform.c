// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2012-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch.h>
#include <err.h>
#include <debug.h>
#include <trace.h>
#include <dev/display.h>
#include <dev/hw_rng.h>
#include <dev/interrupt/arm_gicv2m.h>
#include <dev/timer/arm_generic.h>
#include <dev/uart.h>
#include <lk/init.h>
#include <lib/console.h>
#include <kernel/vm.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <platform.h>
#include <platform/gic.h>
#include <dev/psci.h>
#include <dev/interrupt.h>
#include <arch/arm64/platform.h>
#include <platform/qemu-virt.h>
#include "platform_p.h"

#define DEFAULT_MEMORY_SIZE (MEMSIZE) /* try to fetch from the emulator via the fdt */

static void* ramdisk_base;
static size_t ramdisk_size;

static const paddr_t GICV2M_REG_FRAMES[] = { GICV2M_FRAME_PHYS };

/* initial memory mappings. parsed by start.S */
struct mmu_initial_mapping mmu_initial_mappings[] = {
    /* all of memory */
    {
        .phys = MEMORY_BASE_PHYS,
        .virt = KERNEL_BASE,
        .size = MEMORY_APERTURE_SIZE,
        .flags = 0,
        .name = "memory"
    },

    /* 1GB of peripherals */
    {
        .phys = PERIPHERAL_BASE_PHYS,
        .virt = PERIPHERAL_BASE_VIRT,
        .size = PERIPHERAL_BASE_SIZE,
        .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
        .name = "peripherals"
    },

    /* null entry to terminate the list */
    {}
};

static pmm_arena_info_t arena = {
    .name = "ram",
    .base = MEMORY_BASE_PHYS,
    .size = DEFAULT_MEMORY_SIZE,
    .flags = PMM_ARENA_FLAG_KMAP,
};

void* platform_get_ramdisk(size_t *size) {
    if (ramdisk_base) {
        *size = ramdisk_size;
        return ramdisk_base;
    } else {
        *size = 0;
        return NULL;
    }
}

extern ulong lk_boot_args[4];

void platform_early_init(void)
{
    /* initialize the interrupt controller */
    arm_gicv2m_init(GICV2M_REG_FRAMES, countof(GICV2M_REG_FRAMES));

    arm_generic_timer_init(ARM_GENERIC_TIMER_VIRTUAL_INT, 0);

    uart_init_early();


    // qemu does not put device tree pointer in lk_boot_args,
    // so set it up here before calling read_device_tree
    lk_boot_args[0] = MEMORY_BASE_PHYS;

    read_device_tree(&ramdisk_base, &ramdisk_size, &arena.size);

    /* add the main memory arena */
    pmm_add_arena(&arena);

    /* reserve the first 64k of ram, which should be holding the fdt */
    pmm_alloc_range(MEMBASE, 0x10000 / PAGE_SIZE, NULL);

    platform_preserve_ramdisk();
}

void platform_init(void)
{
    uart_init();
    /* TODO - number of cpus (and topology) should be parsed from device index or command line */

    // 1 cluster with SMP_MAX_CPUS cpus
    uint cluster_cpus[] = { SMP_MAX_CPUS };
    arch_init_cpu_map(countof(cluster_cpus), cluster_cpus);

    /* boot the secondary cpus using the Power State Coordintion Interface */
    for (uint i = 1; i < SMP_MAX_CPUS; i++) {
        arm64_set_secondary_sp(0, i, pmm_alloc_kpages(ARCH_DEFAULT_STACK_SIZE / PAGE_SIZE, NULL, NULL));
        psci_cpu_on(0, i, MEMBASE + KERNEL_LOAD_OFFSET);
    }
}

void platform_halt(platform_halt_action suggested_action, platform_halt_reason reason)
{

    if (suggested_action == HALT_ACTION_REBOOT) {
        psci_system_reset();
    } else if (suggested_action == HALT_ACTION_SHUTDOWN) {
        psci_system_off();
    } else {
#if WITH_PANIC_BACKTRACE
    thread_print_backtrace(get_current_thread(), __GET_FRAME(0));
#endif
#if ENABLE_PANIC_SHELL
        dprintf(ALWAYS, "HALT: starting debug shell... (reason = %u)\n", reason);
        arch_disable_ints();
        panic_shell_start();
#else
        dprintf(ALWAYS, "HALT: spinning forever... (reason = %u)\n", reason);
        arch_disable_ints();
        for (;;);
#endif
    }

    // catch all fallthrough cases
    arch_disable_ints();
    for (;;);
}

/* stub out the hardware rng entropy generator, which doesn't eixst on this platform */
size_t hw_rng_get_entropy(void* buf, size_t len, bool block) {
    return 0;
}

/* no built in framebuffer */
status_t display_get_info(struct display_info *info) {
    return ERR_NOT_FOUND;
}
