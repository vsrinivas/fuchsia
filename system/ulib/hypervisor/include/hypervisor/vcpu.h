// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <magenta/syscalls/hypervisor.h>

typedef struct block block_t;
typedef struct io_apic io_apic_t;
typedef struct io_port io_port_t;
typedef struct pci_bus pci_bus_t;
typedef struct uart uart_t;

/* Stores the state associated with the guest. */
typedef struct guest_ctx {
    io_apic_t* io_apic;
    io_port_t* io_port;
    pci_bus_t* bus;

    uart_t* uart;
    block_t* block;
} guest_ctx_t;

/* Typedefs to abstract reading and writing VCPU state. */
typedef struct vcpu_ctx vcpu_ctx_t;
typedef mx_status_t (*read_state_fn_t)
    (vcpu_ctx_t* vcpu, uint32_t kind, void* buffer, uint32_t len);
typedef mx_status_t (*write_state_fn_t)
    (vcpu_ctx_t* vcpu, uint32_t kind, const void* buffer, uint32_t len);

/* Stores the local APIC state. */
typedef struct local_apic {
    // Address of the local APIC.
    void* apic_addr;
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
mx_status_t vcpu_packet_handler(vcpu_ctx_t* vcpu_ctx, mx_guest_packet_t* packet);

typedef mx_status_t (* device_handler_fn_t)(void* ctx, mx_handle_t vcpu, mx_guest_packet_t* packet);

/* Start asynchronous handling of device operations, based on a trap defined by
 * kind, addr, and len. See mx_guest_set_trap for more details on trap args.
 */
mx_status_t device_async(mx_handle_t vcpu, mx_handle_t guest, uint32_t kind, mx_vaddr_t addr,
                         size_t len, device_handler_fn_t handler, void* ctx);
