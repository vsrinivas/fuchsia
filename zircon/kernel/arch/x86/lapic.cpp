// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <arch/ops.h>
#include <arch/spinlock.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/feature.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/mp.h>
#include <debug.h>
#include <dev/interrupt.h>
#include <err.h>
#include <vm/vm_aspace.h>
#include <zircon/types.h>

#include <lib/console.h>

// We currently only implement support for the xAPIC

// Virtual address of the local APIC's MMIO registers
static void* apic_virt_base;
static bool x2apic_enabled = false;

static uint8_t bsp_apic_id;
static bool bsp_apic_id_valid;

// local apic registers
// set as an offset into the mmio region here
// x2APIC msr offsets are these >> 4
#define LAPIC_REG_ID (0x020)
#define LAPIC_REG_VERSION (0x030)
#define LAPIC_REG_TASK_PRIORITY (0x080)
#define LAPIC_REG_PROCESSOR_PRIORITY (0x0A0)
#define LAPIC_REG_EOI (0x0B0)
#define LAPIC_REG_LOGICAL_DST (0x0D0)
#define LAPIC_REG_SPURIOUS_IRQ (0x0F0)
#define LAPIC_REG_IN_SERVICE(x) (0x100 + ((x) << 4))
#define LAPIC_REG_TRIGGER_MODE(x) (0x180 + ((x) << 4))
#define LAPIC_REG_IRQ_REQUEST(x) (0x200 + ((x) << 4))
#define LAPIC_REG_ERROR_STATUS (0x280)
#define LAPIC_REG_LVT_CMCI (0x2F0)
#define LAPIC_REG_IRQ_CMD_LOW (0x300)
#define LAPIC_REG_IRQ_CMD_HIGH (0x310)
#define LAPIC_REG_LVT_TIMER (0x320)
#define LAPIC_REG_LVT_THERMAL (0x330)
#define LAPIC_REG_LVT_PERF (0x340)
#define LAPIC_REG_LVT_LINT0 (0x350)
#define LAPIC_REG_LVT_LINT1 (0x360)
#define LAPIC_REG_LVT_ERROR (0x370)
#define LAPIC_REG_INIT_COUNT (0x380)
#define LAPIC_REG_CURRENT_COUNT (0x390)
#define LAPIC_REG_DIVIDE_CONF (0x3E0)

#define LAPIC_X2APIC_MSR_BASE (0x800)
#define LAPIC_X2APIC_MSR_ICR (0x830)
#define LAPIC_X2APIC_MSR_SELF_IPI (0x83f)

// Spurious IRQ bitmasks
#define SVR_APIC_ENABLE (1 << 8)
#define SVR_SPURIOUS_VECTOR(x) (x)

// Interrupt Command bitmasks
#define ICR_VECTOR(x) (x)
#define ICR_DELIVERY_PENDING (1 << 12)
#define ICR_LEVEL_ASSERT (1 << 14)
#define ICR_DST(x) (((uint32_t)(x)) << 24)
#define ICR_DST_BROADCAST ICR_DST(0xff)
#define ICR_DELIVERY_MODE(x) (((uint32_t)(x)) << 8)
#define ICR_DST_SHORTHAND(x) (((uint32_t)(x)) << 18)
#define ICR_DST_SELF ICR_DST_SHORTHAND(1)
#define ICR_DST_ALL ICR_DST_SHORTHAND(2)
#define ICR_DST_ALL_MINUS_SELF ICR_DST_SHORTHAND(3)

#define X2_ICR_DST(x) ((uint64_t)(x) << 32)
#define X2_ICR_BROADCAST ((uint64_t)(0xffffffff) << 32)

// Common LVT bitmasks
#define LVT_VECTOR(x) (x)
#define LVT_DELIVERY_MODE(x) (((uint32_t)(x)) << 8)
#define LVT_DELIVERY_PENDING (1 << 12)

static void apic_error_init(void);
static void apic_timer_init(void);
static void apic_pmi_init(void);

static uint32_t lapic_reg_read(size_t offset) {
    if (x2apic_enabled) {
        return read_msr32(LAPIC_X2APIC_MSR_BASE + (uint32_t)(offset >> 4));
    } else {
        return *((volatile uint32_t*)((uintptr_t)apic_virt_base + offset));
    }
}

static void lapic_reg_write(size_t offset, uint32_t val) {
    if (x2apic_enabled) {
        write_msr(LAPIC_X2APIC_MSR_BASE + (uint32_t)(offset >> 4), val);
    } else {
        *((volatile uint32_t*)((uintptr_t)apic_virt_base + offset)) = val;
    }
}

static void lapic_reg_or(size_t offset, uint32_t bits) {
    lapic_reg_write(offset, lapic_reg_read(offset) | bits);
}

static void lapic_reg_and(size_t offset, uint32_t bits) {
    lapic_reg_write(offset, lapic_reg_read(offset) & bits);
}

// This function must be called once on the kernel address space
void apic_vm_init(void) {
    // only memory map the aperture if we're using the legacy mmio interface
    if (!x2apic_enabled) {
        ASSERT(apic_virt_base == nullptr);
        // Create a mapping for the page of MMIO registers
        zx_status_t res = VmAspace::kernel_aspace()->AllocPhysical(
            "lapic",
            PAGE_SIZE,       // size
            &apic_virt_base, // returned virtual address
            PAGE_SIZE_SHIFT, // alignment log2
            APIC_PHYS_BASE,  // physical address
            0,               // vmm flags
            ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE |
                ARCH_MMU_FLAG_UNCACHED_DEVICE); // arch mmu flags
        if (res != ZX_OK) {
            panic("Could not allocate APIC management page: %d\n", res);
        }
        ASSERT(apic_virt_base != nullptr);
    }
}

// Initializes the current processor's local APIC.  Should be called after
// apic_vm_init has been called.
void apic_local_init(void) {
    DEBUG_ASSERT(arch_ints_disabled());

    uint64_t v = read_msr(X86_MSR_IA32_APIC_BASE);

    // if were the boot processor, test and cache x2apic ability
    if (v & IA32_APIC_BASE_BSP) {
        if (x86_feature_test(X86_FEATURE_X2APIC)) {
            dprintf(SPEW, "x2APIC enabled\n");
            x2apic_enabled = true;
        }
    }

    // Enter xAPIC or x2APIC mode and set the base address
    v |= IA32_APIC_BASE_XAPIC_ENABLE;
    v |= x2apic_enabled ? IA32_APIC_BASE_X2APIC_ENABLE : 0;
    write_msr(X86_MSR_IA32_APIC_BASE, v);

    // If this is the bootstrap processor, we should record our APIC ID now
    // that we know it.
    if (v & IA32_APIC_BASE_BSP) {
        uint8_t id = apic_local_id();

        bsp_apic_id = id;
        bsp_apic_id_valid = true;
        x86_set_local_apic_id(id);
    }

    // Specify the spurious interrupt vector and enable the local APIC
    uint32_t svr = SVR_SPURIOUS_VECTOR(X86_INT_APIC_SPURIOUS) | SVR_APIC_ENABLE;
    lapic_reg_write(LAPIC_REG_SPURIOUS_IRQ, svr);

    apic_error_init();
    apic_timer_init();
    apic_pmi_init();
}

uint8_t apic_local_id(void) {
    uint32_t id = lapic_reg_read(LAPIC_REG_ID);

    // legacy apic stores the id in the top 8 bits of the register
    if (!x2apic_enabled)
        id >>= 24;

    // we can only deal with 8 bit apic ids right now
    DEBUG_ASSERT(id < 256);

    return (uint8_t)id;
}

uint8_t apic_bsp_id(void) {
    DEBUG_ASSERT(bsp_apic_id_valid);
    return bsp_apic_id;
}

static inline void apic_wait_for_ipi_send(void) {
    while (lapic_reg_read(LAPIC_REG_IRQ_CMD_LOW) & ICR_DELIVERY_PENDING)
        ;
}

// We only support physical destination modes for now

void apic_send_ipi(
    uint8_t vector,
    uint32_t dst_apic_id,
    enum apic_interrupt_delivery_mode dm) {
    // we only support 8 bit apic ids
    DEBUG_ASSERT(dst_apic_id < UINT8_MAX);

    uint32_t request = ICR_VECTOR(vector) | ICR_LEVEL_ASSERT;
    request |= ICR_DELIVERY_MODE(dm);

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    if (x2apic_enabled) {
        write_msr(LAPIC_X2APIC_MSR_ICR, X2_ICR_DST(dst_apic_id) | request);
    } else {
        lapic_reg_write(LAPIC_REG_IRQ_CMD_HIGH, ICR_DST(dst_apic_id));
        lapic_reg_write(LAPIC_REG_IRQ_CMD_LOW, request);
        apic_wait_for_ipi_send();
    }
    arch_interrupt_restore(state, 0);
}

void apic_send_self_ipi(uint8_t vector, enum apic_interrupt_delivery_mode dm) {
    uint32_t request = ICR_VECTOR(vector) | ICR_LEVEL_ASSERT;
    request |= ICR_DELIVERY_MODE(dm) | ICR_DST_SELF;

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    if (x2apic_enabled) {
        // special register for triggering self ipis
        write_msr(LAPIC_X2APIC_MSR_SELF_IPI, vector);
    } else {
        lapic_reg_write(LAPIC_REG_IRQ_CMD_LOW, request);
        apic_wait_for_ipi_send();
    }
    arch_interrupt_restore(state, 0);
}

// Broadcast to everyone including self
void apic_send_broadcast_self_ipi(
    uint8_t vector,
    enum apic_interrupt_delivery_mode dm) {
    uint32_t request = ICR_VECTOR(vector) | ICR_LEVEL_ASSERT;
    request |= ICR_DELIVERY_MODE(dm) | ICR_DST_ALL;

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    if (x2apic_enabled) {
        write_msr(LAPIC_X2APIC_MSR_ICR, X2_ICR_BROADCAST | request);
    } else {
        lapic_reg_write(LAPIC_REG_IRQ_CMD_HIGH, ICR_DST_BROADCAST);
        lapic_reg_write(LAPIC_REG_IRQ_CMD_LOW, request);
        apic_wait_for_ipi_send();
    }
    arch_interrupt_restore(state, 0);
}

// Broadcast to everyone excluding self
void apic_send_broadcast_ipi(
    uint8_t vector,
    enum apic_interrupt_delivery_mode dm) {
    uint32_t request = ICR_VECTOR(vector) | ICR_LEVEL_ASSERT;
    request |= ICR_DELIVERY_MODE(dm) | ICR_DST_ALL_MINUS_SELF;

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    if (x2apic_enabled) {
        write_msr(LAPIC_X2APIC_MSR_ICR, X2_ICR_BROADCAST | request);
    } else {
        lapic_reg_write(LAPIC_REG_IRQ_CMD_HIGH, ICR_DST_BROADCAST);
        lapic_reg_write(LAPIC_REG_IRQ_CMD_LOW, request);
        apic_wait_for_ipi_send();
    }
    arch_interrupt_restore(state, 0);
}

void apic_issue_eoi(void) {
    // Write 0 to the EOI address to issue an EOI
    lapic_reg_write(LAPIC_REG_EOI, 0);
}

// If this function returns an error, timer state will not have
// been changed.
static zx_status_t apic_timer_set_divide_value(uint8_t v) {
    uint32_t new_value = 0;
    switch (v) {
    case 1:
        new_value = 0xb;
        break;
    case 2:
        new_value = 0x0;
        break;
    case 4:
        new_value = 0x1;
        break;
    case 8:
        new_value = 0x2;
        break;
    case 16:
        new_value = 0x3;
        break;
    case 32:
        new_value = 0x8;
        break;
    case 64:
        new_value = 0x9;
        break;
    case 128:
        new_value = 0xa;
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }
    lapic_reg_write(LAPIC_REG_DIVIDE_CONF, new_value);
    return ZX_OK;
}

static void apic_timer_init(void) {
    lapic_reg_write(LAPIC_REG_LVT_TIMER, LVT_VECTOR(X86_INT_APIC_TIMER) | LVT_MASKED);
}

// Racy; primarily useful for calibrating the timer.
uint32_t apic_timer_current_count(void) {
    return lapic_reg_read(LAPIC_REG_CURRENT_COUNT);
}

void apic_timer_mask(void) {
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    lapic_reg_or(LAPIC_REG_LVT_TIMER, LVT_MASKED);
    arch_interrupt_restore(state, 0);
}

void apic_timer_unmask(void) {
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    lapic_reg_and(LAPIC_REG_LVT_TIMER, ~LVT_MASKED);
    arch_interrupt_restore(state, 0);
}

void apic_timer_stop(void) {
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    lapic_reg_write(LAPIC_REG_INIT_COUNT, 0);
    if (x86_feature_test(X86_FEATURE_TSC_DEADLINE)) {
        write_msr(X86_MSR_IA32_TSC_DEADLINE, 0);
    }
    arch_interrupt_restore(state, 0);
}

zx_status_t apic_timer_set_oneshot(uint32_t count, uint8_t divisor, bool masked) {
    zx_status_t status = ZX_OK;
    uint32_t timer_config = LVT_VECTOR(X86_INT_APIC_TIMER) |
                            LVT_TIMER_MODE_ONESHOT;
    if (masked) {
        timer_config |= LVT_MASKED;
    }

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);

    status = apic_timer_set_divide_value(divisor);
    if (status != ZX_OK) {
        goto cleanup;
    }
    lapic_reg_write(LAPIC_REG_LVT_TIMER, timer_config);
    lapic_reg_write(LAPIC_REG_INIT_COUNT, count);
cleanup:
    arch_interrupt_restore(state, 0);
    return status;
}

void apic_timer_set_tsc_deadline(uint64_t deadline, bool masked) {
    DEBUG_ASSERT(x86_feature_test(X86_FEATURE_TSC_DEADLINE));

    uint32_t timer_config = LVT_VECTOR(X86_INT_APIC_TIMER) |
                            LVT_TIMER_MODE_TSC_DEADLINE;
    if (masked) {
        timer_config |= LVT_MASKED;
    }

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);

    lapic_reg_write(LAPIC_REG_LVT_TIMER, timer_config);
    // Intel recommends using an MFENCE to ensure the LVT_TIMER_ADDR write
    // takes before the write_msr(), since writes to this MSR are ignored if the
    // time mode is not DEADLINE.
    mb();
    write_msr(X86_MSR_IA32_TSC_DEADLINE, deadline);

    arch_interrupt_restore(state, 0);
}

zx_status_t apic_timer_set_periodic(uint32_t count, uint8_t divisor) {
    zx_status_t status = ZX_OK;
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);

    status = apic_timer_set_divide_value(divisor);
    if (status != ZX_OK) {
        goto cleanup;
    }
    lapic_reg_write(LAPIC_REG_LVT_TIMER, LVT_VECTOR(X86_INT_APIC_TIMER) | LVT_TIMER_MODE_PERIODIC);
    lapic_reg_write(LAPIC_REG_INIT_COUNT, count);
cleanup:
    arch_interrupt_restore(state, 0);
    return status;
}

void apic_timer_interrupt_handler(void) {
    platform_handle_apic_timer_tick();
}

static void apic_error_init(void) {
    lapic_reg_write(LAPIC_REG_LVT_ERROR, LVT_VECTOR(X86_INT_APIC_ERROR));
    // Re-arm the error interrupt triggering mechanism
    lapic_reg_write(LAPIC_REG_ERROR_STATUS, 0);
}

void apic_error_interrupt_handler(void) {
    DEBUG_ASSERT(arch_ints_disabled());

    // This write doesn't effect the subsequent read, but is required prior to
    // reading.
    lapic_reg_write(LAPIC_REG_ERROR_STATUS, 0);
    panic("APIC error detected: %u\n", lapic_reg_read(LAPIC_REG_ERROR_STATUS));
}

static void apic_pmi_init(void) {
    lapic_reg_write(LAPIC_REG_LVT_PERF, LVT_VECTOR(X86_INT_APIC_PMI) | LVT_MASKED);
}

void apic_pmi_mask(void) {
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    lapic_reg_or(LAPIC_REG_LVT_PERF, LVT_MASKED);
    arch_interrupt_restore(state, 0);
}

void apic_pmi_unmask(void) {
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    lapic_reg_and(LAPIC_REG_LVT_PERF, ~LVT_MASKED);
    arch_interrupt_restore(state, 0);
}

static int cmd_apic(int argc, const cmd_args* argv, uint32_t flags) {
    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments\n");
    usage:
        printf("usage:\n");
        printf("%s dump io\n", argv[0].str);
        printf("%s dump local\n", argv[0].str);
        printf("%s broadcast <vec>\n", argv[0].str);
        printf("%s self <vec>\n", argv[0].str);
        return ZX_ERR_INTERNAL;
    }

    if (!strcmp(argv[1].str, "broadcast")) {
        if (argc < 3)
            goto notenoughargs;
        uint8_t vec = (uint8_t)argv[2].u;
        apic_send_broadcast_ipi(vec, DELIVERY_MODE_FIXED);
        printf("irr: %x\n", lapic_reg_read(LAPIC_REG_IRQ_REQUEST(vec / 32)));
        printf("isr: %x\n", lapic_reg_read(LAPIC_REG_IN_SERVICE(vec / 32)));
        printf("icr: %x\n", lapic_reg_read(LAPIC_REG_IRQ_CMD_LOW));
    } else if (!strcmp(argv[1].str, "self")) {
        if (argc < 3)
            goto notenoughargs;
        uint8_t vec = (uint8_t)argv[2].u;
        apic_send_self_ipi(vec, DELIVERY_MODE_FIXED);
        printf("irr: %x\n", lapic_reg_read(LAPIC_REG_IRQ_REQUEST(vec / 32)));
        printf("isr: %x\n", lapic_reg_read(LAPIC_REG_IN_SERVICE(vec / 32)));
        printf("icr: %x\n", lapic_reg_read(LAPIC_REG_IRQ_CMD_LOW));
    } else if (!strcmp(argv[1].str, "dump")) {
        if (argc < 3)
            goto notenoughargs;
        if (!strcmp(argv[2].str, "local")) {
            printf("Caution: this is only for one CPU\n");
            apic_local_debug();
        } else if (!strcmp(argv[2].str, "io")) {
            apic_io_debug();
        } else {
            printf("unknown subcommand\n");
            goto usage;
        }
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return ZX_OK;
}

void apic_local_debug(void) {
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);

    printf("apic %02x:\n", apic_local_id());
    printf("  version: %08x:\n", lapic_reg_read(LAPIC_REG_VERSION));
    printf("  logical_dst: %08x\n", lapic_reg_read(LAPIC_REG_LOGICAL_DST));
    printf("  spurious_irq: %08x\n", lapic_reg_read(LAPIC_REG_SPURIOUS_IRQ));
    printf("  tpr: %02x\n", (uint8_t)lapic_reg_read(LAPIC_REG_TASK_PRIORITY));
    printf("  ppr: %02x\n", (uint8_t)lapic_reg_read(LAPIC_REG_PROCESSOR_PRIORITY));
    for (int i = 0; i < 8; ++i)
        printf("  irr %d: %08x\n", i, lapic_reg_read(LAPIC_REG_IRQ_REQUEST(i)));
    for (int i = 0; i < 8; ++i)
        printf("  isr %d: %08x\n", i, lapic_reg_read(LAPIC_REG_IN_SERVICE(i)));

    arch_interrupt_restore(state, 0);
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("apic", "apic commands", &cmd_apic)
#endif
STATIC_COMMAND_END(apic);
