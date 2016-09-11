// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#include <err.h>
#include <trace.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu_topology.h>
#include <arch/x86/mmu.h>
#include <platform.h>
#include "platform_p.h"
#include <platform/pc.h>
#include <platform/pc/acpi.h>
#include <platform/console.h>
#include <platform/keyboard.h>
#include <dev/pcie.h>
#include <dev/uart.h>
#include <arch/mmu.h>
#include <arch/mp.h>
#include <arch/x86.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include <lk/init.h>
#include <kernel/cmdline.h>
#include <kernel/vm.h>

#define LOCAL_TRACE 0

extern status_t x86_alloc_msi_block(uint requested_irqs,
                                    bool can_target_64bit,
                                    bool is_msix,
                                    pcie_msi_block_t* out_block);
extern void x86_free_msi_block(pcie_msi_block_t* block);
extern void x86_register_msi_handler(const pcie_msi_block_t* block,
                                     uint                    msi_id,
                                     int_handler             handler,
                                     void*                   ctx);

#if WITH_KERNEL_VM
struct mmu_initial_mapping mmu_initial_mappings[] = {
#if ARCH_X86_64
    /* 64GB of memory mapped where the kernel lives */
    {
        .phys = MEMBASE,
        .virt = KERNEL_ASPACE_BASE,
        .size = 64ULL*GB, /* x86-64 maps first 64GB by default */
        .flags = 0,
        .name = "memory"
    },
#endif
    /* KERNEL_SIZE of memory mapped where the kernel lives.
     * On x86-64, this only sticks around until the VM is brought up, after
     * that this will be replaced with mappings of the correct privileges. */
    {
        .phys = MEMBASE,
        .virt = KERNEL_BASE,
        .size = KERNEL_SIZE, /* x86 maps first KERNEL_SIZE by default */
#if ARCH_X86_64
        .flags = MMU_INITIAL_MAPPING_TEMPORARY,
        .name = "kernel_temp"
#else
        .flags = 0,
        .name = "kernel",
#endif
    },
    /* null entry to terminate the list */
    { 0 }
};
#endif

void *_zero_page_boot_params;

uint32_t bootloader_acpi_rsdp;
uint32_t bootloader_fb_base;
uint32_t bootloader_fb_width;
uint32_t bootloader_fb_height;
uint32_t bootloader_fb_stride;
uint32_t bootloader_fb_format;
uint32_t bootloader_i915_reg_base;
uint32_t bootloader_fb_window_size;

static uint32_t bootloader_ramdisk_base;
static uint32_t bootloader_ramdisk_size;

static bool early_console_disabled;

/* This is a temporary extension to the "zero page" protcol, making use
 * of obsolete fields to pass some additional data from bootloader to
 * kernel in a way that would to badly interact with a Linux kernel booting
 * from the same loader.
 */
static void platform_save_bootloader_data(void)
{
    uint32_t *zp = (void*) ((uintptr_t)_zero_page_boot_params + KERNEL_BASE);

    bootloader_ramdisk_base = zp[0x218 / 4];
    bootloader_ramdisk_size = zp[0x21C / 4];

    if (zp[0x228/4] != 0) {
        cmdline_init((void*) X86_PHYS_TO_VIRT(zp[0x228/4]));
    }

    if (zp[0x220 / 4] == 0xDBC64323) {
        bootloader_acpi_rsdp = zp[0x80 / 4];
        bootloader_fb_base = zp[0x90 / 4];
        bootloader_fb_width = zp[0x94 / 4];
        bootloader_fb_height = zp[0x98 / 4];
        bootloader_fb_stride = zp[0x9C / 4];
        bootloader_fb_format = zp[0xA0 / 4];
        bootloader_i915_reg_base = zp[0xA4 / 4];
        bootloader_fb_window_size = zp[0xA8 / 4];
    }
}

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

#include <dev/display.h>
#include <lib/gfxconsole.h>

void *boot_alloc_mem(size_t len);

status_t display_get_info(struct display_info *info) {
    return gfxconsole_display_get_info(info);
}

static void platform_early_display_init(void) {
    struct display_info info;
    void *bits;

    if (bootloader_fb_base == 0) {
        return;
    }
    if (cmdline_get_bool("gfxconsole.early", false) == false) {
        early_console_disabled = true;
        return;
    }

    // allocate an offscreen buffer of worst-case size, page aligned
    bits = boot_alloc_mem(8192 + bootloader_fb_height * bootloader_fb_stride * 4);
    bits = (void*) ((((uintptr_t) bits) + 4095) & (~4095));

    memset(&info, 0, sizeof(info));
    info.format = bootloader_fb_format;
    info.width = bootloader_fb_width;
    info.height = bootloader_fb_height;
    info.stride = bootloader_fb_stride;
    info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;
    info.framebuffer = (void*) X86_PHYS_TO_VIRT(bootloader_fb_base);

    gfxconsole_bind_display(&info, bits);
}

/* Ensure the framebuffer is write-combining as soon as we have the VMM.
 * Some system firmware has the MTRRs for the framebuffer set to Uncached.
 * Since dealing with MTRRs is rather complicated, we wait for the VMM to
 * come up so we can use PAT to manage the memory types. */
static void platform_ensure_display_memtype(uint level)
{
    if (bootloader_fb_base == 0) {
        return;
    }
    if (early_console_disabled) {
        return;
    }
    struct display_info info;
    memset(&info, 0, sizeof(info));
    info.format = bootloader_fb_format;
    info.width = bootloader_fb_width;
    info.height = bootloader_fb_height;
    info.stride = bootloader_fb_stride;
    info.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;

    void *addr = NULL;
    status_t status = vmm_alloc_physical(
            vmm_get_kernel_aspace(),
            "boot_fb",
            ROUNDUP(info.stride * info.height * 4, PAGE_SIZE),
            &addr,
            PAGE_SIZE_SHIFT,
            0 /* min alloc gap */,
            bootloader_fb_base,
            0 /* vmm flags */,
            ARCH_MMU_FLAG_WRITE_COMBINING | ARCH_MMU_FLAG_PERM_READ |
                ARCH_MMU_FLAG_PERM_WRITE);
    if (status != NO_ERROR) {
        TRACEF("Failed to map boot_fb: %d\n", status);
        return;
    }

    info.framebuffer = addr;
    gfxconsole_bind_display(&info, NULL);
}
LK_INIT_HOOK(display_memtype, &platform_ensure_display_memtype, LK_INIT_LEVEL_VM + 1);

void platform_early_init(void)
{
    /* get the debug output working */
    platform_init_debug_early();

    /* get the text console working */
    platform_init_console();

    /* extract "zero page" data while still accessible */
    platform_save_bootloader_data();

    /* if the bootloader has framebuffer info, use it for early console */
    platform_early_display_init();

    /* initialize physical memory arenas */
    platform_mem_init();

    platform_preserve_ramdisk();
}

#if WITH_SMP
static void platform_init_smp(void)
{
    uint32_t num_cpus = 0;

    status_t status = platform_enumerate_cpus(NULL, 0, &num_cpus);
    if (status != NO_ERROR) {
        TRACEF("failed to enumerate CPUs, disabling SMP\n");
        return;
    }

    // allocate 2x the table for temporary work
    uint32_t *apic_ids = malloc(sizeof(*apic_ids) * num_cpus * 2);
    if (apic_ids == NULL) {
        TRACEF("failed to allocate apic_ids table, disabling SMP\n");
        return;
    }

    // a temporary list used before we filter out hyperthreaded pairs
    uint32_t *apic_ids_temp = apic_ids + num_cpus;

    // find the list of all cpu apic ids into a temporary list
    uint32_t real_num_cpus;
    status = platform_enumerate_cpus(apic_ids_temp, num_cpus, &real_num_cpus);
    if (status != NO_ERROR || num_cpus != real_num_cpus) {
        TRACEF("failed to enumerate CPUs, disabling SMP\n");
        free(apic_ids);
        return;
    }

    // Filter out hyperthreads if we've been told not to init them
    bool use_ht = cmdline_get_bool("smp.ht", true);

    // we're implicitly running on the BSP
    uint32_t bsp_apic_id = apic_local_id();

    // iterate over all the cores and optionally disable some of them
    dprintf(INFO, "cpu topology:\n");
    uint32_t using_count = 0;
    for (uint32_t i = 0; i < num_cpus; ++i) {
        x86_cpu_topology_t topo;
        x86_cpu_topology_decode(apic_ids_temp[i], &topo);

        // filter it out if it's a HT pair that we dont want to use
        bool keep = true;
        if (!use_ht && topo.smt_id != 0)
            keep = false;

        dprintf(INFO, "\t%u: apic id 0x%x package %u core %u smt %u%s%s\n",
                i, apic_ids_temp[i], topo.package_id, topo.core_id, topo.smt_id,
                (apic_ids_temp[i] == bsp_apic_id) ? " BSP" : "",
                keep ? "" : " (not using)");

        if (!keep)
            continue;

        // save this apic id into the primary list
        apic_ids[using_count++] = apic_ids_temp[i];
    }
    num_cpus = using_count;

    // Find the CPU count limit
    uint32_t max_cpus = cmdline_get_uint32("smp.maxcpus", SMP_MAX_CPUS);
    if (max_cpus > SMP_MAX_CPUS || max_cpus <= 0) {
        printf("invalid smp.maxcpus value, defaulting to %d\n", SMP_MAX_CPUS);
        max_cpus = SMP_MAX_CPUS;
    }

    dprintf(INFO, "Found %u cpus\n", num_cpus);
    if (num_cpus > max_cpus) {
        TRACEF("Clamping number of CPUs to %u\n", max_cpus);
        num_cpus = max_cpus;
    }

    if (num_cpus == max_cpus || !use_ht) {
        // If we are at the max number of CPUs, or have filtered out
        // hyperthreads, sanity check that the bootstrap processor is in the set.
        bool found_bp = false;
        for (unsigned int i = 0; i < num_cpus; ++i) {
            if (apic_ids[i] == bsp_apic_id) {
                found_bp = true;
                break;
            }
        }
        ASSERT(found_bp);
    }

    x86_init_smp(apic_ids, num_cpus);

    for (uint i = 0; i < num_cpus - 1; ++i) {
        if (apic_ids[i] == bsp_apic_id) {
            apic_ids[i] = apic_ids[num_cpus - 1];
            apic_ids[num_cpus - 1] = bsp_apic_id;
            break;
        }
    }
    x86_bringup_aps(apic_ids, num_cpus - 1);

    free(apic_ids);
}

status_t platform_mp_prep_cpu_unplug(uint cpu_id)
{
    // TODO: Make sure the IOAPIC and PCI have nothing for this CPU
    return arch_mp_prep_cpu_unplug(cpu_id);
}

#endif

void platform_pcie_init_info(pcie_init_info_t *out)
{
    *out = (pcie_init_info_t){
        .ecam_windows         = NULL,
        .ecam_window_count    = 0,
        .mmio_window_lo       = { .bus_addr = pcie_mem_lo_base, .size = pcie_mem_lo_size },
        .mmio_window_hi       = { .bus_addr = 0,                .size = 0 },
        .pio_window           = { .bus_addr = pcie_pio_base,    .size = pcie_pio_size },
        .legacy_irq_swizzle   = NULL,
        .alloc_msi_block      = x86_alloc_msi_block,
        .free_msi_block       = x86_free_msi_block,
        .register_msi_handler = x86_register_msi_handler,
        .mask_unmask_msi      = NULL,
    };
}

void platform_init(void)
{
    platform_init_debug();

#if NO_USER_KEYBOARD
    platform_init_keyboard(&console_input_buf);
#endif

#if WITH_SMP
    platform_init_smp();
#endif

    if (!early_console_disabled) {
        // detach the early console - pcie init may move it
        gfxconsole_bind_display(NULL, NULL);
    }
}
