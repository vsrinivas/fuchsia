// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <dev/interrupt.h>
#include <kernel/vm.h>

__BEGIN_CDECLS

#define INVALID_APIC_ID             0xffffffff
#define APIC_PHYS_BASE              0xfee00000
#define IA32_APIC_BASE_BSP          (1u << 8)
#define IA32_APIC_BASE_XAPIC_ENABLE (1u << 11)
#define NUM_ISA_IRQS                16

// LVT Timer bitmasks
#define LVT_TIMER_VECTOR_MASK       0x000000ff
#define LVT_TIMER_MODE_MASK         0x00060000
#define LVT_TIMER_MODE_ONESHOT      (0u << 17)
#define LVT_TIMER_MODE_PERIODIC     (1u << 17)
#define LVT_TIMER_MODE_TSC_DEADLINE (2u << 17)

enum apic_interrupt_delivery_mode {
    // Unless you know what you're doing, you want FIXED.
    DELIVERY_MODE_FIXED = 0,
    DELIVERY_MODE_LOWEST_PRI = 1,
    DELIVERY_MODE_SMI = 2,
    DELIVERY_MODE_NMI = 4,
    DELIVERY_MODE_INIT = 5,
    DELIVERY_MODE_STARTUP = 6,
    DELIVERY_MODE_EXT_INT = 7,
};

enum apic_interrupt_dst_mode {
    DST_MODE_PHYSICAL = 0,
    DST_MODE_LOGICAL = 1,
};

// Functionality provided by the local APIC
void apic_vm_init(void);
void apic_local_init(void);
uint8_t apic_local_id(void);
void apic_irq_set(unsigned int vector, bool enable);
void apic_send_ipi(
        uint8_t vector,
        uint32_t dst_apic_id,
        enum apic_interrupt_delivery_mode dm);
void apic_send_self_ipi(uint8_t vector, enum apic_interrupt_delivery_mode dm);
void apic_send_broadcast_ipi(
        uint8_t vector,
        enum apic_interrupt_delivery_mode dm);
void apic_send_broadcast_self_ipi(
        uint8_t vector,
        enum apic_interrupt_delivery_mode dm);
void apic_issue_eoi(void);

status_t apic_timer_set_oneshot(uint32_t count, uint8_t divisor, bool masked);
void apic_timer_set_tsc_deadline(uint64_t deadline, bool masked);
status_t apic_timer_set_periodic(uint32_t count, uint8_t divisor);
uint32_t apic_timer_current_count(void);
void apic_timer_mask(void);
void apic_timer_unmask(void);
void apic_timer_stop(void);

enum handler_return apic_error_interrupt_handler(void);
enum handler_return apic_timer_interrupt_handler(void);

// platform code needs to implement this
enum handler_return platform_handle_apic_timer_tick(void);

// Information about the system IO APICs
struct io_apic_descriptor {
    uint8_t apic_id;
    // virtual IRQ base for ACPI
    uint32_t global_irq_base;
    // Physical address of the base of this IOAPIC's MMIO
    paddr_t paddr;
};

// Information describing an ISA override.  An override can change the
// global IRQ number and/or change bus signaling characteristics
// for the specified ISA IRQ.
struct io_apic_isa_override {
    uint8_t isa_irq;
    bool remapped;
    enum interrupt_trigger_mode tm;
    enum interrupt_polarity pol;
    uint32_t global_irq;
};

// Functionality provided by the IO APICs
#define IO_APIC_IOREGSEL    0x00
#define IO_APIC_IOWIN       0x10

#define IO_APIC_REG_ID      0x00
#define IO_APIC_REG_VER     0x01
#define IO_APIC_IRQ_MASK    true
#define IO_APIC_IRQ_UNMASK  false

void apic_io_init(
        struct io_apic_descriptor *io_apics_descs,
        unsigned int num_io_apics,
        struct io_apic_isa_override *overrides,
        unsigned int num_overrides);
bool apic_io_is_valid_irq(uint32_t global_irq);
void apic_io_mask_irq(uint32_t global_irq, bool mask);
void apic_io_configure_irq(
        uint32_t global_irq,
        enum interrupt_trigger_mode trig_mode,
        enum interrupt_polarity polarity,
        enum apic_interrupt_delivery_mode del_mode,
        bool mask,
        enum apic_interrupt_dst_mode dst_mode,
        uint8_t dst,
        uint8_t vector);
status_t apic_io_fetch_irq_config(
        uint32_t global_irq,
        enum interrupt_trigger_mode* trig_mode,
        enum interrupt_polarity* polarity);
void apic_io_configure_irq_vector(
        uint32_t global_irq,
        uint8_t vector);
uint8_t apic_io_fetch_irq_vector(uint32_t global_irq);

void apic_io_mask_isa_irq(uint8_t isa_irq, bool mask);
// For ISA configuration, we don't need to specify the trigger mode
// and polarity since we initialize these to match the ISA bus or
// any overrides we've been told about.
void apic_io_configure_isa_irq(
        uint8_t isa_irq,
        enum apic_interrupt_delivery_mode del_mode,
        bool mask,
        enum apic_interrupt_dst_mode dst_mode,
        uint8_t dst,
        uint8_t vector);
void apic_io_issue_eoi(uint32_t global_irq, uint8_t vec);
uint32_t apic_io_isa_to_global(uint8_t isa_irq);

void apic_local_debug(void);
void apic_io_debug(void);

__END_CDECLS
