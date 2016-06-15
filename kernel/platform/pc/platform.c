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
#include <arch/x86/mmu.h>
#include <platform.h>
#include "platform_p.h"
#include <platform/acpi.h>
#include <platform/pc.h>
#include <platform/console.h>
#include <platform/keyboard.h>
#include <dev/interrupt_event.h>
#include <dev/pcie.h>
#include <dev/uart.h>
#include <arch/x86.h>
#include <arch/mmu.h>
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include <kernel/vm.h>

#define LOCAL_TRACE 0

extern void pci_init(void);
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

static struct acpi_pcie_irq_mapping pcie_root_irq_map;

void *_zero_page_boot_params;

uint32_t bootloader_acpi_rsdp;
uint32_t bootloader_fb_base;
uint32_t bootloader_fb_width;
uint32_t bootloader_fb_height;
uint32_t bootloader_fb_stride;
uint32_t bootloader_fb_format;
uint32_t bootloader_i915_reg_base;
uint32_t bootloader_fb_window_size;

/* This is a temporary extension to the "zero page" protcol, making use
 * of obsolete fields to pass some additional data from bootloader to
 * kernel in a way that would to badly interact with a Linux kernel booting
 * from the same loader.
 */
void platform_save_bootloader_data(void)
{
    uint32_t *zp = (void*) ((uintptr_t)_zero_page_boot_params + KERNEL_BASE);

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

#include <dev/display.h>
#include <lib/gfxconsole.h>

void *boot_alloc_mem(size_t len);

status_t display_get_info(struct display_info *info) {
    return gfxconsole_display_get_info(info);
}

void platform_early_display_init(void) {
    struct display_info info;
    void *bits;

    if (bootloader_fb_base == 0) {
        return;
    }

    // allocate an offscreen buffer of worst-case size, page aligned
    bits = boot_alloc_mem(8192 + bootloader_fb_height * bootloader_fb_width * 4);
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
}

#if WITH_SMP
void platform_init_smp(void)
{
    uint32_t num_cpus = 0;
    status_t status = platform_enumerate_cpus(NULL, 0, &num_cpus);
    if (status != NO_ERROR) {
        TRACEF("failed to enumerate CPUs, disabling SMP\n");
        return;
    }

    printf("Found %d cpus\n", num_cpus);
    if (num_cpus > SMP_MAX_CPUS) {
        TRACEF("Clamping number of CPUs to %d\n", SMP_MAX_CPUS);
        num_cpus = SMP_MAX_CPUS;
    }

    uint32_t *apic_ids = malloc(sizeof(*apic_ids) * num_cpus);
    if (apic_ids == NULL) {
        TRACEF("failed to allocate apic_ids table, disabling SMP\n");
        return;
    }
    uint32_t real_num_cpus;
    status = platform_enumerate_cpus(apic_ids, num_cpus, &real_num_cpus);
    if (status != NO_ERROR) {
        TRACEF("failed to enumerate CPUs, disabling SMP\n");
        free(apic_ids);
        return;
    }

    if (num_cpus == SMP_MAX_CPUS) {
        // If we are at the max number of CPUs, sanity check that the bootstrap
        // processor is in that set, to make sure clamping didn't go awry.
        uint32_t bsp_apic_id = apic_local_id();
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
    free(apic_ids);
}
#endif

static status_t acpi_pcie_irq_swizzle(const pcie_common_state_t* common,
                                      uint pin,
                                      uint *irq)
{
    DEBUG_ASSERT(common);
    DEBUG_ASSERT(pin < 4);
    if (common->bus_id != 0) {
        return ERR_NOT_FOUND;
    }
    uint32_t val = pcie_root_irq_map.dev_pin_to_global_irq[common->dev_id][common->func_id][pin];
    if (val == ACPI_NO_IRQ_MAPPING) {
        return ERR_NOT_FOUND;
    }
    *irq = val;
    return NO_ERROR;
}

void platform_init_pcie(void) {
    struct acpi_pcie_config config;
    status_t status = platform_find_pcie_config(&config);
    if (status != NO_ERROR) {
        TRACEF("failed to find PCIe configuration space\n");
        return;
    }
    if (config.start_bus != 0) {
        TRACEF("PCIe buses that don't start at 0 not currently supported\n");
        return;
    }
    if (config.segment_group != 0) {
        TRACEF("PCIe segment groups not currently supported\n");
        return;
    }

    status = platform_find_pcie_legacy_irq_mapping(&pcie_root_irq_map);
    if (status != NO_ERROR) {
        TRACEF("failed to find PCIe IRQ remapping\n");
        return;
    }

    // Configure the discovered PCIe IRQs
    for (uint i = 0; i < pcie_root_irq_map.num_irqs; ++i) {
        struct acpi_irq_signal *sig = &pcie_root_irq_map.irqs[i];
        enum io_apic_irq_trigger_mode trig_mode = IRQ_TRIGGER_MODE_EDGE;
        enum io_apic_irq_polarity polarity = IRQ_POLARITY_ACTIVE_LOW;
        if (sig->active_high) {
            polarity = IRQ_POLARITY_ACTIVE_HIGH;
        }
        if (sig->level_triggered) {
            trig_mode = IRQ_TRIGGER_MODE_LEVEL;
        }
        apic_io_configure_irq(
                sig->global_irq,
                trig_mode,
                polarity,
                DELIVERY_MODE_FIXED,
                IO_APIC_IRQ_MASK,
                DST_MODE_PHYSICAL,
                // TODO(teisenbe): Balance IRQs
                apic_local_id(),
                0);
    }

    // Check for a quirk that we've seen.  Some systems will report overly large
    // PCIe config regions that collide with architectural registers.
    paddr_t end = config.ecam_phys +
            (config.end_bus - config.start_bus + 1) * PCIE_ECAM_BYTE_PER_BUS;
    DEBUG_ASSERT(config.start_bus <= config.end_bus);
    if (end > HIGH_ADDRESS_LIMIT) {
        TRACEF("PCIe config space collides with arch devices, truncating\n");
        end = HIGH_ADDRESS_LIMIT;
        DEBUG_ASSERT(end >= config.ecam_phys);
        config.ecam_size = ROUNDDOWN(end - config.ecam_phys, PCIE_ECAM_BYTE_PER_BUS);
        config.end_bus = (config.ecam_size / PCIE_ECAM_BYTE_PER_BUS) + config.start_bus - 1;
    }

    // TODO(johngro): Do not limit this to a single range.  Instead, fetch all
    // of the ECAM ranges from ACPI, as well as the appropriate bus start/end
    // ranges.
    DEBUG_ASSERT(config.ecam_size >= PCIE_ECAM_BYTE_PER_BUS);
    const pcie_ecam_range_t PCIE_ECAM_WINDOWS[] = {
        {
            .io_range  = { .bus_addr = config.ecam_phys, .size = config.ecam_size },
            .bus_start = 0x00,
            .bus_end   = (uint8_t)(config.ecam_size / PCIE_ECAM_BYTE_PER_BUS) - 1,
        },
    };

    const pcie_init_info_t PCIE_INIT_INFO = {
        .ecam_windows         = PCIE_ECAM_WINDOWS,
        .ecam_window_count    = countof(PCIE_ECAM_WINDOWS),
        .mmio_window_lo       = { .bus_addr = pcie_mem_lo_base, .size = pcie_mem_lo_size },
        .mmio_window_hi       = { .bus_addr = 0,                .size = 0 },
        .pio_window           = { .bus_addr = pcie_pio_base,    .size = pcie_pio_size },
        .legacy_irq_swizzle   = acpi_pcie_irq_swizzle,
        .alloc_msi_block      = x86_alloc_msi_block,
        .free_msi_block       = x86_free_msi_block,
        .register_msi_handler = x86_register_msi_handler,
        .mask_unmask_msi      = NULL,
    };

    status = pcie_init(&PCIE_INIT_INFO);
    if (status != NO_ERROR)
        TRACEF("Failed to initialize PCIe bus driver! (status = %d)\n", status);
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

    platform_init_acpi();

    // detach the early console - pcie init may move it
    gfxconsole_bind_display(NULL, NULL);

    platform_init_pcie();
}
