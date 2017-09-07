// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <platform/pc/hpet.h>

#include <bits.h>
#include <err.h>
#include <lk/init.h>
#include <kernel/auto_lock.h>
#include <kernel/spinlock.h>
#include <vm/vm_aspace.h>
#include <fbl/algorithm.h>

struct hpet_timer_registers {
    volatile uint64_t conf_caps;
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

static spin_lock_t lock = SPIN_LOCK_INITIAL_VALUE;

static struct acpi_hpet_descriptor hpet_desc;
static bool hpet_present = false;
static struct hpet_registers *hpet_regs;
uint64_t _hpet_ticks_per_ms;
static uint64_t tick_period_in_fs;
static uint8_t num_timers;

/* Minimum number of ticks ahead a oneshot timer needs to be.  Targetted
 * to be 100ns */
static uint64_t min_ticks_ahead;

#define MAX_PERIOD_IN_FS 0x05F5E100ULL

/* Bit masks for the general_config register */
#define GEN_CONF_EN 1

/* Bit masks for the per-time conf_caps register */
#define TIMER_CONF_LEVEL_TRIGGERED (1ULL<<1)
#define TIMER_CONF_INT_EN (1ULL<<2)
#define TIMER_CONF_PERIODIC (1ULL<<3)
#define TIMER_CAP_PERIODIC(reg) BIT_SET(reg, 4)
#define TIMER_CAP_64BIT(reg) BIT_SET(reg, 5)
#define TIMER_CONF_PERIODIC_SET_COUNT (1ULL<<6)
#define TIMER_CONF_IRQ(n) ((uint64_t)((n) & (0x1f))<<9)
#define TIMER_CAP_IRQS(reg) static_cast<uint32_t>(BITS_SHIFT(reg, 63, 32))

static void hpet_init(uint level)
{
    status_t status = platform_find_hpet(&hpet_desc);
    if (status != MX_OK) {
        return;
    }

    if (hpet_desc.port_io) {
        return;
    }

    status_t res = VmAspace::kernel_aspace()->AllocPhysical(
            "hpet",
            PAGE_SIZE, /* size */
            (void **)&hpet_regs, /* returned virtual address */
            PAGE_SIZE_SHIFT, /* alignment log2 */
            (paddr_t)hpet_desc.address, /* physical address */
            0, /* vmm flags */
            ARCH_MMU_FLAG_UNCACHED_DEVICE | ARCH_MMU_FLAG_PERM_READ |
                ARCH_MMU_FLAG_PERM_WRITE);
    if (res != MX_OK) {
        return;
    }

    bool has_64bit_count = BIT_SET(hpet_regs->general_caps, 13);
    tick_period_in_fs = hpet_regs->general_caps >> 32;
    if (tick_period_in_fs == 0 || tick_period_in_fs > MAX_PERIOD_IN_FS) {
        goto fail;
    }

    /* We only support HPETs that are 64-bit and have at least two timers */
    num_timers = static_cast<uint8_t>(BITS_SHIFT(hpet_regs->general_caps, 12, 8) + 1);
    if (!has_64bit_count || num_timers < 2) {
        goto fail;
    }

    /* Make sure all timers have interrupts disabled */
    for (uint8_t i = 0; i < num_timers; ++i) {
        hpet_regs->timers[i].conf_caps &= ~TIMER_CONF_INT_EN;
    }

    _hpet_ticks_per_ms = 1000000000000ULL / tick_period_in_fs;
    min_ticks_ahead = 100000000ULL / tick_period_in_fs;
    hpet_present = true;
    return;

fail:
    VmAspace::kernel_aspace()->FreeRegion(reinterpret_cast<vaddr_t>(hpet_regs));
    hpet_regs = nullptr;
    num_timers = 0;
}
/* Begin running after ACPI tables are up */
LK_INIT_HOOK(hpet, hpet_init, LK_INIT_LEVEL_VM + 2);

status_t hpet_timer_disable(uint n)
{
    if (unlikely(n >= num_timers)) {
        return MX_ERR_NOT_SUPPORTED;
    }

    AutoSpinLock guard(&lock);
    hpet_regs->timers[n].conf_caps &= ~TIMER_CONF_INT_EN;

    return MX_OK;
}

uint64_t hpet_get_value(void)
{
    uint64_t v = hpet_regs->main_counter_value;
    uint64_t v2 = hpet_regs->main_counter_value;
    /* Even though the specification says it should not be necessary to read
     * multiple times, we have observed that QEMU converts the 64-bit
     * memory access in to two 32-bit accesses, resulting in bad reads. QEMU
     * reads the low 32-bits first, so the result is a large jump when it
     * wraps 32 bits.  To work around this, we return the lesser of two reads.
     */
    return fbl::min(v, v2);
}

status_t hpet_set_value(uint64_t v)
{
    AutoSpinLock guard(&lock);

    if (hpet_regs->general_config & GEN_CONF_EN) {
        return MX_ERR_BAD_STATE;
    }

    hpet_regs->main_counter_value = v;
    return MX_OK;
}

status_t hpet_timer_configure_irq(uint n, uint irq)
{
    if (unlikely(n >= num_timers)) {
        return MX_ERR_NOT_SUPPORTED;
    }

    AutoSpinLock guard(&lock);

    uint32_t irq_bitmap = TIMER_CAP_IRQS(hpet_regs->timers[n].conf_caps);
    if (irq >= 32 || !BIT_SET(irq_bitmap, irq)) {
        return MX_ERR_NOT_SUPPORTED;
    }

    uint64_t conf = hpet_regs->timers[n].conf_caps;
    conf &= ~TIMER_CONF_IRQ(~0ULL);
    conf |= TIMER_CONF_IRQ(irq);
    hpet_regs->timers[n].conf_caps = conf;

    return MX_OK;
}

status_t hpet_timer_set_oneshot(uint n, uint64_t deadline)
{
    if (unlikely(n >= num_timers)) {
        return MX_ERR_NOT_SUPPORTED;
    }

    AutoSpinLock guard(&lock);

    uint64_t difference = deadline - hpet_get_value();
    if (unlikely(difference > (1ULL>>63))) {
        /* Either this is a very long timer, or we wrapped around */
        return MX_ERR_INVALID_ARGS;
    }
    if (unlikely(difference < min_ticks_ahead)) {
        return MX_ERR_INVALID_ARGS;
    }

    hpet_regs->timers[n].conf_caps &= ~(TIMER_CONF_PERIODIC |
            TIMER_CONF_PERIODIC_SET_COUNT);
    hpet_regs->timers[n].comparator_value = deadline;
    hpet_regs->timers[n].conf_caps |= TIMER_CONF_INT_EN;

    return MX_OK;
}

status_t hpet_timer_set_periodic(uint n, uint64_t period)
{
    if (unlikely(n >= num_timers)) {
        return MX_ERR_NOT_SUPPORTED;
    }

    AutoSpinLock guard(&lock);

    if (!TIMER_CAP_PERIODIC(hpet_regs->timers[n].conf_caps)) {
        return MX_ERR_NOT_SUPPORTED;
    }

    /* It's unsafe to set a periodic timer while the hpet is running or the
     * main counter value is not 0. */
    if ((hpet_regs->general_config & GEN_CONF_EN) ||
        hpet_regs->main_counter_value != 0ULL) {
        return MX_ERR_BAD_STATE;
    }

    hpet_regs->timers[n].conf_caps |= TIMER_CONF_PERIODIC |
            TIMER_CONF_PERIODIC_SET_COUNT;
    hpet_regs->timers[n].comparator_value = period;
    hpet_regs->timers[n].conf_caps |= TIMER_CONF_INT_EN;

    return MX_OK;
}

bool hpet_is_present(void)
{
    return hpet_present;
}

void hpet_enable(void)
{
    DEBUG_ASSERT(hpet_is_present());

    AutoSpinLock guard(&lock);
    hpet_regs->general_config |= GEN_CONF_EN;
}

void hpet_disable(void)
{
    DEBUG_ASSERT(hpet_is_present());

    AutoSpinLock guard(&lock);
    hpet_regs->general_config &= ~GEN_CONF_EN;
}

/* Blocks for the requested number of milliseconds.
 * For use in calibration */
void hpet_wait_ms(uint16_t ms)
{
    uint64_t init_timer_value = hpet_regs->main_counter_value;
    uint64_t target = (uint64_t)ms * _hpet_ticks_per_ms;
    while (hpet_regs->main_counter_value - init_timer_value <= target);
}
