// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/* This file implements a very basic HPET interface.  Really it is just enough
 * to calibrate the TSC and the APIC timer. It is NOT THREADSAFE. */

#include <kernel/vm.h>
#include <lk/init.h>
#include <platform/pc/hpet.h>

struct hpet_timer_registers {
    volatile uint64_t caps;
    volatile uint64_t comparator_value;
    volatile uint64_t fsb_int_route;
    uint8_t _reserved[8];
} __PACKED;

struct hpet_registers {
    volatile uint64_t general_caps;
    uint8_t _reserved0[8];
    volatile uint64_t general_config;
    uint8_t _reserved1[8];
    volatile uint64_t general_int_status;
    uint8_t _reserved2[0xf0-0x28];
    volatile uint64_t main_counter_value;
    uint8_t _reserved3[8];
    struct hpet_timer_registers timers[];
} __PACKED;

static struct acpi_hpet_descriptor hpet_desc;
static bool hpet_present = false;
static struct hpet_registers *hpet_regs;
static uint64_t ticks_per_ms;
static uint64_t tick_period_in_fs;

#define MAX_PERIOD_IN_FS 0x05F5E100ULL

static void hpet_init(uint level)
{
    status_t status = platform_find_hpet(&hpet_desc);
    if (status != NO_ERROR) {
        return;
    }

    if (hpet_desc.port_io) {
        return;
    }

    vmm_aspace_t *kernel_aspace = vmm_get_kernel_aspace();
    status_t res = vmm_alloc_physical(
            kernel_aspace,
            "hpet",
            PAGE_SIZE, /* size */
            (void **)&hpet_regs, /* returned virtual address */
            PAGE_SIZE_SHIFT, /* alignment log2 */
            hpet_desc.address, /* physical address */
            0, /* vmm flags */
            ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_READ |
                ARCH_MMU_FLAG_PERM_WRITE);
    if (res != NO_ERROR) {
        return;
    }

    tick_period_in_fs = hpet_regs->general_caps >> 32;
    if (tick_period_in_fs == 0 || tick_period_in_fs > MAX_PERIOD_IN_FS) {
        vmm_free_region(kernel_aspace, (vaddr_t)hpet_regs);
        hpet_regs = NULL;
        return;
    }

    ticks_per_ms = 1000000000000ULL / tick_period_in_fs;
    hpet_present = true;
}
/* Begin running after ACPI tables are up */
LK_INIT_HOOK(hpet, hpet_init, LK_INIT_LEVEL_VM + 2);

bool hpet_is_present(void)
{
    return hpet_present;
}

void hpet_enable(void)
{
    DEBUG_ASSERT(hpet_is_present());
    hpet_regs->general_config |= 1;
}

void hpet_disable(void)
{
    DEBUG_ASSERT(hpet_is_present());
    hpet_regs->general_config &= ~1;
}

/* Blocks for the requested number of milliseconds.
 * For use in calibration */
void hpet_wait_ms(uint16_t ms)
{
    uint64_t init_timer_value = hpet_regs->main_counter_value;
    uint64_t target = (uint64_t)ms * ticks_per_ms;
    while (hpet_regs->main_counter_value - init_timer_value <= target);
}
