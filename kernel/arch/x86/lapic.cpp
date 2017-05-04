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
#include <err.h>
#include <kernel/vm/vm_aspace.h>
#include <dev/interrupt.h>

#include <lib/console.h>

// We currently only implement support for the xAPIC

// Initialization MSR
#define IA32_APIC_BASE_X2APIC_ENABLE (1 << 10)

// Virtual address of the local APIC's MMIO registers
static void *apic_virt_base;

#define LAPIC_ID_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x020))
#define LAPIC_VERSION_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x030))
#define TASK_PRIORITY_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x080))
#define PROCESSOR_PRIORITY_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x0A0))
#define EOI_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x0B0))
#define LOGICAL_DST_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x0D0))
#define SPURIOUS_IRQ_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x0F0))
#define IN_SERVICE_ADDR(x) \
        ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x100 + ((x) << 4)))
#define TRIGGER_MODE_ADDR(x) \
        ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x180 + ((x) << 4)))
#define IRQ_REQUEST_ADDR(x) \
        ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x200 + ((x) << 4)))
#define ERROR_STATUS_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x280))
#define LVT_CMCI_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x2F0))
#define IRQ_CMD_LOW_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x300))
#define IRQ_CMD_HIGH_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x310))
#define LVT_TIMER_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x320))
#define LVT_THERMAL_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x330))
#define LVT_PERF_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x340))
#define LVT_LINT0_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x350))
#define LVT_LINT1_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x360))
#define LVT_ERROR_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x370))
#define INIT_COUNT_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x380))
#define CURRENT_COUNT_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x390))
#define DIVIDE_CONF_ADDR ((volatile uint32_t *)((uintptr_t)apic_virt_base + 0x3E0))

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

// Common LVT bitmasks
#define LVT_VECTOR(x) (x)
#define LVT_DELIVERY_MODE(x) (((uint32_t)(x)) << 8)
#define LVT_DELIVERY_PENDING (1 << 12)
#define LVT_MASKED (1 << 16)

static void apic_error_init(void);
static void apic_timer_init(void);

// This function must be called once on the kernel address space
void apic_vm_init(void)
{
    ASSERT(apic_virt_base == NULL);
    // Create a mapping for the page of MMIO registers
    status_t res = VmAspace::kernel_aspace()->AllocPhysical(
            "lapic",
            PAGE_SIZE, // size
            &apic_virt_base, // returned virtual address
            PAGE_SIZE_SHIFT, // alignment log2
            APIC_PHYS_BASE, // physical address
            0, // vmm flags
            ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE |
                ARCH_MMU_FLAG_UNCACHED_DEVICE); // arch mmu flags
    if (res != NO_ERROR) {
        panic("Could not allocate APIC management page: %d\n", res);
    }
    ASSERT(apic_virt_base != NULL);
}

// Initializes the current processor's local APIC.  Should be called after
// apic_vm_init has been called.
void apic_local_init(void)
{
    DEBUG_ASSERT(arch_ints_disabled());

    // Enter XAPIC mode and set the base address
    uint64_t v = read_msr(X86_MSR_IA32_APIC_BASE);
    v |= IA32_APIC_BASE_XAPIC_ENABLE;
    write_msr(X86_MSR_IA32_APIC_BASE, v);

    // If this is the bootstrap processor, we should record our APIC ID now
    // that we know it.
    if (v & IA32_APIC_BASE_BSP) {
        x86_set_local_apic_id(apic_local_id());
    }

    // Specify the spurious interrupt vector and enable the local APIC
    uint32_t svr = SVR_SPURIOUS_VECTOR(X86_INT_APIC_SPURIOUS) | SVR_APIC_ENABLE;
    *SPURIOUS_IRQ_ADDR = svr;

    apic_error_init();
    apic_timer_init();
}

uint8_t apic_local_id(void)
{
    return (uint8_t)(*LAPIC_ID_ADDR >> 24);
}

static inline void apic_wait_for_ipi_send(void) {
    while (*IRQ_CMD_LOW_ADDR & ICR_DELIVERY_PENDING);
}

// We only support physical destination modes for now

void apic_send_ipi(
        uint8_t vector,
        uint32_t dst_apic_id,
        enum apic_interrupt_delivery_mode dm)
{
    // we only support 8 bit apic ids
    DEBUG_ASSERT(dst_apic_id < UINT8_MAX);

    uint32_t request = ICR_VECTOR(vector) | ICR_LEVEL_ASSERT;
    request |= ICR_DELIVERY_MODE(dm);

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    *IRQ_CMD_HIGH_ADDR = ICR_DST(dst_apic_id);
    *IRQ_CMD_LOW_ADDR = request;
    apic_wait_for_ipi_send();
    arch_interrupt_restore(state, 0);
}

void apic_send_self_ipi(uint8_t vector, enum apic_interrupt_delivery_mode dm)
{
    uint32_t request = ICR_VECTOR(vector) | ICR_LEVEL_ASSERT;
    request |= ICR_DELIVERY_MODE(dm) | ICR_DST_SELF;

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    *IRQ_CMD_LOW_ADDR = request;
    apic_wait_for_ipi_send();
    arch_interrupt_restore(state, 0);
}

// Broadcast to everyone including self
void apic_send_broadcast_self_ipi(
        uint8_t vector,
        enum apic_interrupt_delivery_mode dm)
{
    uint32_t request = ICR_VECTOR(vector) | ICR_LEVEL_ASSERT;
    request |= ICR_DELIVERY_MODE(dm) | ICR_DST_ALL;

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    *IRQ_CMD_HIGH_ADDR = ICR_DST_BROADCAST;
    *IRQ_CMD_LOW_ADDR = request;
    apic_wait_for_ipi_send();
    arch_interrupt_restore(state, 0);
}

// Broadcast to everyone excluding self
void apic_send_broadcast_ipi(
        uint8_t vector,
        enum apic_interrupt_delivery_mode dm)
{
    uint32_t request = ICR_VECTOR(vector) | ICR_LEVEL_ASSERT;
    request |= ICR_DELIVERY_MODE(dm) | ICR_DST_ALL_MINUS_SELF;

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    *IRQ_CMD_HIGH_ADDR = ICR_DST_BROADCAST;
    *IRQ_CMD_LOW_ADDR = request;
    apic_wait_for_ipi_send();
    arch_interrupt_restore(state, 0);
}

void apic_issue_eoi(void)
{
    // Write any value to the EOI address to issue an EOI
    *EOI_ADDR = 1;
}

// If this function returns an error, timer state will not have
// been changed.
static status_t apic_timer_set_divide_value(uint8_t v) {
    uint32_t new_value = 0;
    switch (v) {
        case 1: new_value = 0xb; break;
        case 2: new_value = 0x0; break;
        case 4: new_value = 0x1; break;
        case 8: new_value = 0x2; break;
        case 16: new_value = 0x3; break;
        case 32: new_value = 0x8; break;
        case 64: new_value = 0x9; break;
        case 128: new_value = 0xa; break;
        default: return ERR_INVALID_ARGS;
    }
    *DIVIDE_CONF_ADDR = new_value;
    return NO_ERROR;
}

static void apic_timer_init(void) {
    *LVT_TIMER_ADDR = LVT_VECTOR(X86_INT_APIC_TIMER) | LVT_MASKED;
}

// Racy; primarily useful for calibrating the timer.
uint32_t apic_timer_current_count(void) {
    return *CURRENT_COUNT_ADDR;
}

void apic_timer_mask(void) {
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    *LVT_TIMER_ADDR |= LVT_MASKED;
    arch_interrupt_restore(state, 0);
}

void apic_timer_unmask(void) {
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    *LVT_TIMER_ADDR &= ~LVT_MASKED;
    arch_interrupt_restore(state, 0);
}

void apic_timer_stop(void) {
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    *INIT_COUNT_ADDR = 0;
    if (x86_feature_test(X86_FEATURE_TSC_DEADLINE)) {
        write_msr(X86_MSR_IA32_TSC_DEADLINE, 0);
    }
    arch_interrupt_restore(state, 0);
}

status_t apic_timer_set_oneshot(uint32_t count, uint8_t divisor, bool masked) {
    status_t status = NO_ERROR;
    uint32_t timer_config = LVT_VECTOR(X86_INT_APIC_TIMER) |
            LVT_TIMER_MODE_ONESHOT;
    if (masked) {
        timer_config |= masked;
    }

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);

    status = apic_timer_set_divide_value(divisor);
    if (status != NO_ERROR) {
        goto cleanup;
    }
    *LVT_TIMER_ADDR = timer_config;
    *INIT_COUNT_ADDR = count;
cleanup:
    arch_interrupt_restore(state, 0);
    return status;
}

void apic_timer_set_tsc_deadline(uint64_t deadline, bool masked) {
    DEBUG_ASSERT(x86_feature_test(X86_FEATURE_TSC_DEADLINE));

    uint32_t timer_config = LVT_VECTOR(X86_INT_APIC_TIMER) |
            LVT_TIMER_MODE_TSC_DEADLINE;
    if (masked) {
        timer_config |= masked;
    }

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);

    *LVT_TIMER_ADDR = timer_config;
    // Intel recommends using an MFENCE to ensure the LVT_TIMER_ADDR write
    // takes before the write_msr(), since writes to this MSR are ignored if the
    // time mode is not DEADLINE.
    mb();
    write_msr(X86_MSR_IA32_TSC_DEADLINE, deadline);

    arch_interrupt_restore(state, 0);
}

status_t apic_timer_set_periodic(uint32_t count, uint8_t divisor) {
    status_t status = NO_ERROR;
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);

    status = apic_timer_set_divide_value(divisor);
    if (status != NO_ERROR) {
        goto cleanup;
    }
    *LVT_TIMER_ADDR = LVT_VECTOR(X86_INT_APIC_TIMER) | LVT_TIMER_MODE_PERIODIC;
    *INIT_COUNT_ADDR = count;
cleanup:
    arch_interrupt_restore(state, 0);
    return status;
}

enum handler_return apic_timer_interrupt_handler(void) {
    return platform_handle_apic_timer_tick();
}

static void apic_error_init(void) {
    *LVT_ERROR_ADDR = LVT_VECTOR(X86_INT_APIC_ERROR);
    // Re-arm the error interrupt triggering mechanism
    *ERROR_STATUS_ADDR = 0;
}

enum handler_return apic_error_interrupt_handler(void) {
    DEBUG_ASSERT(arch_ints_disabled());

    // This write doesn't effect the subsequent read, but is required prior to
    // reading.
    *ERROR_STATUS_ADDR = 0;
    panic("APIC error detected: %u\n", *ERROR_STATUS_ADDR);
}

static int cmd_apic(int argc, const cmd_args *argv, uint32_t flags)
{
    if (argc < 2) {
notenoughargs:
        printf("not enough arguments\n");
usage:
        printf("usage:\n");
        printf("%s dump io\n", argv[0].str);
        printf("%s dump local\n", argv[0].str);
        printf("%s broadcast <vec>\n", argv[0].str);
        printf("%s self <vec>\n", argv[0].str);
        return ERR_INTERNAL;
    }

    if (!strcmp(argv[1].str, "broadcast")) {
        if (argc < 3)
          goto notenoughargs;
        uint8_t vec = (uint8_t)argv[2].u;
        apic_send_broadcast_ipi(vec, DELIVERY_MODE_FIXED);
        printf("irr: %x\n", *IRQ_REQUEST_ADDR(vec / 32));
        printf("isr: %x\n", *IN_SERVICE_ADDR(vec / 32));
        printf("icr: %x\n", *IRQ_CMD_LOW_ADDR);
    } else if (!strcmp(argv[1].str, "self")) {
        if (argc < 3)
          goto notenoughargs;
        uint8_t vec = (uint8_t)argv[2].u;
        apic_send_self_ipi(vec, DELIVERY_MODE_FIXED);
        printf("irr: %x\n", *IRQ_REQUEST_ADDR(vec / 32));
        printf("isr: %x\n", *IN_SERVICE_ADDR(vec / 32));
        printf("icr: %x\n", *IRQ_CMD_LOW_ADDR);
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

    return NO_ERROR;
}

void apic_local_debug(void)
{
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);

    printf("apic %02x:\n", apic_local_id());
    printf("  version: %08x:\n", *LAPIC_VERSION_ADDR);
    printf("  logical_dst: %08x\n", *LOGICAL_DST_ADDR);
    printf("  spurious_irq: %08x\n", *SPURIOUS_IRQ_ADDR);
    printf("  tpr: %02x\n", (uint8_t)*TASK_PRIORITY_ADDR);
    printf("  ppr: %02x\n", (uint8_t)*PROCESSOR_PRIORITY_ADDR);
    for (int i = 0; i < 8; ++i)
        printf("  irr %d: %08x\n", i, *IRQ_REQUEST_ADDR(i));
    for (int i = 0; i < 8; ++i)
        printf("  isr %d: %08x\n", i, *IN_SERVICE_ADDR(i));

    arch_interrupt_restore(state, 0);
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("apic", "apic commands", &cmd_apic)
#endif
STATIC_COMMAND_END(apic);
