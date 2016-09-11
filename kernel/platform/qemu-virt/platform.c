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
#include <dev/interrupt/arm_gicv2m_msi.h>
#include <dev/pcie.h>
#include <dev/timer/arm_generic.h>
#include <dev/uart.h>
#include <lk/init.h>
#include <lib/console.h>
#include <kernel/cmdline.h>
#include <kernel/vm.h>
#include <kernel/spinlock.h>
#include <platform.h>
#include <platform/gic.h>
#include <dev/interrupt.h>
#include <platform/qemu-virt.h>
#include <libfdt.h>
#include "platform_p.h"

#define DEFAULT_MEMORY_SIZE (MEMSIZE) /* try to fetch from the emulator via the fdt */

static const pcie_ecam_range_t PCIE_ECAM_WINDOWS[] = {
    {
        .io_range  = { .bus_addr = PCIE_ECAM_BASE_PHYS, .size = PCIE_ECAM_SIZE },
        .bus_start = 0x00,
        .bus_end   = (uint8_t)(PCIE_ECAM_SIZE / PCIE_ECAM_BYTE_PER_BUS) - 1,
    },
};

static const paddr_t GICV2M_REG_FRAMES[] = { GICV2M_FRAME_PHYS };

static status_t qemu_pcie_irq_swizzle(const pcie_device_state_t* dev,
                                      uint pin,
                                      uint *irq)
{
    DEBUG_ASSERT(dev && irq);
    DEBUG_ASSERT(pin < PCIE_MAX_LEGACY_IRQ_PINS);

    if (dev->bus_id != 0)
        return ERR_NOT_FOUND;

    *irq = PCIE_INT_BASE + ((pin + dev->dev_id) % PCIE_MAX_LEGACY_IRQ_PINS);
    return NO_ERROR;
}

static pcie_init_info_t PCIE_INIT_INFO = {
    .ecam_windows         = PCIE_ECAM_WINDOWS,
    .ecam_window_count    = countof(PCIE_ECAM_WINDOWS),
    .mmio_window_lo       = { .bus_addr = PCIE_MMIO_BASE_PHYS, .size = PCIE_MMIO_SIZE },
    .mmio_window_hi       = { .bus_addr = 0,                   .size = 0 },
    .pio_window           = { .bus_addr = PCIE_PIO_BASE_PHYS,  .size = PCIE_PIO_SIZE },
    .legacy_irq_swizzle   = qemu_pcie_irq_swizzle,
    .alloc_msi_block      = arm_gicv2m_alloc_msi_block,
    .free_msi_block       = arm_gicv2m_free_msi_block,
    .register_msi_handler = arm_gicv2m_register_msi_handler,
    .mask_unmask_msi      = arm_gicv2m_mask_unmask_msi,
};

void platform_pcie_init_info(pcie_init_info_t *out)
{
    memcpy(out, &PCIE_INIT_INFO, sizeof(PCIE_INIT_INFO));
}

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
    { 0 }
};

static pmm_arena_info_t arena = {
    .name = "ram",
    .base = MEMORY_BASE_PHYS,
    .size = DEFAULT_MEMORY_SIZE,
    .flags = PMM_ARENA_FLAG_KMAP,
};

extern void psci_call(ulong arg0, ulong arg1, ulong arg2, ulong arg3);

static uint32_t bootloader_ramdisk_base;
static uint32_t bootloader_ramdisk_size;
static void* ramdisk_base;
static size_t ramdisk_size;

static void platform_preserve_ramdisk(void) {
    if (bootloader_ramdisk_size == 0) {
        return;
    }
    if (bootloader_ramdisk_base == 0) {
        return;
    }
    struct list_node list = LIST_INITIAL_VALUE(list);
    size_t pages = (bootloader_ramdisk_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t actual = pmm_alloc_range(bootloader_ramdisk_base, pages, &list);
    if (actual != pages) {
        panic("unable to reserve ramdisk memory range\n");
    }

    // mark all of the pages we allocated as WIRED
    vm_page_t *p;
    list_for_every_entry(&list, p, vm_page_t, free.node) {
        p->state = VM_PAGE_STATE_WIRED;
    }

    ramdisk_base = paddr_to_kvaddr(bootloader_ramdisk_base);
    ramdisk_size = pages * PAGE_SIZE;
}

void* platform_get_ramdisk(size_t *size) {
    if (ramdisk_base) {
        *size = ramdisk_size;
        return ramdisk_base;
    } else {
        *size = 0;
        return NULL;
    }
}

void platform_early_init(void)
{
    /* initialize the interrupt controller */
    arm_gicv2m_init(GICV2M_REG_FRAMES, countof(GICV2M_REG_FRAMES));

    arm_generic_timer_init(ARM_GENERIC_TIMER_PHYSICAL_INT, 0);

    uart_init_early();

    /* look for a flattened device tree just before the kernel */
    const void *fdt = (void *)KERNEL_BASE;
    int err = fdt_check_header(fdt);
    if (err >= 0) {
        /* walk the nodes, looking for 'memory' and 'chosen' */
        int depth = 0;
        int offset = 0;
        for (;;) {
            offset = fdt_next_node(fdt, offset, &depth);
            if (offset < 0)
                break;

            /* get the name */
            const char *name = fdt_get_name(fdt, offset, NULL);
            if (!name)
                continue;

            /* look for the properties we care about */
            if (strcmp(name, "memory") == 0) {
                int lenp;
                const void *prop_ptr = fdt_getprop(fdt, offset, "reg", &lenp);
                if (prop_ptr && lenp == 0x10) {
                    /* we're looking at a memory descriptor */
                    //uint64_t base = fdt64_to_cpu(*(uint64_t *)prop_ptr);
                    uint64_t len = fdt64_to_cpu(*((const uint64_t *)prop_ptr + 1));

                    /* trim size on certain platforms */
#if ARCH_ARM
                    if (len > 1024*1024*1024U) {
                        len = 1024*1024*1024; /* only use the first 1GB on ARM32 */
                        printf("trimming memory to 1GB\n");
                    }
#endif

                    /* set the size in the pmm arena */
                    arena.size = len;
                }
            } else if (strcmp(name, "chosen") == 0) {
                int lenp;
                const void *prop_ptr = fdt_getprop(fdt, offset, "bootargs", &lenp);
                if (prop_ptr) {
                    cmdline_init(prop_ptr);
                }

                prop_ptr = fdt_getprop(fdt, offset, "linux,initrd-start", &lenp);
                if (prop_ptr && lenp == 4) {
                    bootloader_ramdisk_base = fdt32_to_cpu(*(const uint32_t*)prop_ptr);
                }

                uint32_t initrd_end = 0;
                prop_ptr = fdt_getprop(fdt, offset, "linux,initrd-end", &lenp);
                if (prop_ptr && lenp == 4) {
                    initrd_end = fdt32_to_cpu(*(const uint32_t*)prop_ptr);
                }

                if (bootloader_ramdisk_base && initrd_end <= bootloader_ramdisk_base) {
                    printf("invalid initrd args: 0x%08x < 0x%08x\n", initrd_end, bootloader_ramdisk_base);
                    bootloader_ramdisk_base = 0;
                } else {
                    bootloader_ramdisk_size = initrd_end - bootloader_ramdisk_base;
                }
            }
        }
    }

    /* add the main memory arena */
    pmm_add_arena(&arena);

    /* reserve the first 64k of ram, which should be holding the fdt */
    pmm_alloc_range(MEMBASE, 0x10000 / PAGE_SIZE, NULL);

    platform_preserve_ramdisk();

    /* boot the secondary cpus using the Power State Coordintion Interface */
    ulong psci_call_num = 0x84000000 + 3; /* SMC32 CPU_ON */
#if ARCH_ARM64
    psci_call_num += 0x40000000; /* SMC64 */
#endif
    for (uint i = 1; i < SMP_MAX_CPUS; i++) {
        psci_call(psci_call_num, i, MEMBASE + KERNEL_LOAD_OFFSET, 0);
    }
}

void platform_init(void)
{
    uart_init();

    /* Initialize the MSI allocator */
    status_t ret = arm_gic2vm_msi_init();
    if (ret != NO_ERROR) {
        TRACEF("Failed to initialize MSI allocator (ret = %d).  PCI will be "
               "restricted to legacy IRQ mode.\n", ret);
        PCIE_INIT_INFO.alloc_msi_block = NULL;
        PCIE_INIT_INFO.free_msi_block  = NULL;
    }

    /* Tell the PCIe subsystem where it can find its resources. */
    status_t status = pcie_init(&PCIE_INIT_INFO);
    if (status != NO_ERROR)
        TRACEF("Failed to initialize PCIe bus driver! (status = %d)\n", status);
}

void platform_halt(platform_halt_action suggested_action, platform_halt_reason reason)
{

    if (suggested_action == HALT_ACTION_REBOOT) {
        ulong psci_call_num = 0x84000000 + 9; /* SYSTEM_RESET */
        psci_call(psci_call_num, 0, 0, 0);
    } else if (suggested_action == HALT_ACTION_SHUTDOWN) {
        ulong psci_call_num = 0x84000000 + 8; /* SYSTEM_SHUTDOWN */
        psci_call(psci_call_num, 0, 0, 0);
    } else {
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


