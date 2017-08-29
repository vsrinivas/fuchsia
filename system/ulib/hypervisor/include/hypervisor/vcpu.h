// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <magenta/syscalls/hypervisor.h>

__BEGIN_CDECLS

typedef struct io_apic io_apic_t;
typedef struct io_port io_port_t;
typedef struct mx_port_packet mx_port_packet_t;
typedef struct pci_bus pci_bus_t;
typedef struct uart uart_t;

/* Stores the state associated with the guest. */
typedef struct guest_ctx {
    io_apic_t* io_apic;
    io_port_t* io_port;
    pci_bus_t* bus;

    uart_t* uart;
} guest_ctx_t;

/* Typedefs to abstract reading and writing VCPU state. */
typedef struct vcpu_ctx vcpu_ctx_t;
typedef mx_status_t (*read_state_fn_t)(vcpu_ctx_t* vcpu, uint32_t kind, void* buffer, uint32_t len);
typedef mx_status_t (*write_state_fn_t)(vcpu_ctx_t* vcpu, uint32_t kind, const void* buffer, uint32_t len);

/* Local APIC registers are all 128-bit aligned. */
typedef union local_apic_reg {
    uint32_t data[4];
    uint32_t u32;
} __PACKED local_apic_reg_t;

/* Local APIC register map. */
typedef struct local_apic_regs {
    local_apic_reg_t reserved1[2];

    local_apic_reg_t id;      // Read/Write.
    local_apic_reg_t version; // Read Only.

    local_apic_reg_t reserved2[4];

    local_apic_reg_t tpr;    // Read/Write.
    local_apic_reg_t apr;    // Read Only.
    local_apic_reg_t ppr;    // Read Only.
    local_apic_reg_t eoi;    // Write Only.
    local_apic_reg_t rrd;    // Read Only.
    local_apic_reg_t ldr;    // Read/Write.
    local_apic_reg_t dfr;    // Read/Write.
    local_apic_reg_t isr[8]; // Read Only.
    local_apic_reg_t tmr[8]; // Read Only.
    local_apic_reg_t irr[8]; // Read Only.
    local_apic_reg_t esr;    // Read Only.
} __PACKED local_apic_regs_t;

/* Stores the local APIC state. */
typedef struct local_apic {
    // VCPU associated with this APIC.
    mx_handle_t vcpu;
    union {
        // Address of the local APIC.
        void* apic_addr;
        // Register accessors.
        local_apic_regs_t* regs;
    };
} local_apic_t;

/* Stores the state associated with a single VCPU. */
typedef struct vcpu_ctx {
    mx_handle_t vcpu;

    read_state_fn_t read_state;
    write_state_fn_t write_state;

    local_apic_t local_apic;
    guest_ctx_t* guest_ctx;
} vcpu_ctx_t;

/* Initializes a VCPU context. */
void vcpu_init(vcpu_ctx_t* vcpu_ctx);

/* Controls execution of a VCPU context, providing the main logic. */
mx_status_t vcpu_loop(vcpu_ctx_t* vcpu_ctx);

/* Processes a single guest packet. */
mx_status_t vcpu_packet_handler(vcpu_ctx_t* vcpu_ctx, mx_port_packet_t* packet);

typedef mx_status_t (*device_handler_fn_t)(mx_port_packet_t* packet, void* ctx);

/* A set of arguments to specify a trap region.
 *
 * See mx_guest_set_trap for more details on trap args.
 */
typedef struct trap_args {
    uint32_t kind;
    mx_vaddr_t addr;
    size_t len;
    uint32_t key;
} trap_args_t;

/* Start asynchronous handling of device operations, based on a set of traps
 * provided. A trap will be created for every trap in |traps|.
 */
mx_status_t device_async(mx_handle_t guest, const trap_args_t* traps, size_t num_traps,
                         device_handler_fn_t handler, void* ctx);

__END_CDECLS
