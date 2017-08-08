// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <magenta/syscalls/hypervisor.h>
#include <magenta/types.h>

typedef struct block block_t;
typedef struct io_apic io_apic_t;
typedef struct io_port io_port_t;
typedef struct pci_device pci_device_t;
typedef struct uart uart_t;

/* Stores the state associated with the guest. */
typedef struct guest_state {
    mx_handle_t guest;
    mtx_t mutex;

    // Guest memory.
    void* mem_addr;
    size_t mem_size;

    io_apic_t* io_apic;
    io_port_t* io_port;
    pci_device_t* bus;

    uart_t* uart;
    block_t* block;
} guest_state_t;

/* Typedefs to abstract reading and writing VCPU state. */
typedef struct vcpu_context vcpu_context_t;
typedef mx_status_t (*read_state_fn_t)
    (vcpu_context_t* vcpu, uint32_t kind, void* buffer, uint32_t len);
typedef mx_status_t (*write_state_fn_t)
    (vcpu_context_t* vcpu, uint32_t kind, const void* buffer, uint32_t len);

/* Stores the local APIC state. */
typedef struct local_apic {
    // Address of the local APIC.
    void* apic_addr;
} local_apic_t;

/* Stores the state associated with a single VCPU. */
typedef struct vcpu_context {
    mx_handle_t vcpu;

    read_state_fn_t read_state;
    write_state_fn_t write_state;

    local_apic_t local_apic;
    guest_state_t* guest_state;
} vcpu_context_t;

/* Initializes a VCPU context. */
void vcpu_init(vcpu_context_t* context);

/* Controls execution of a VCPU context, providing the main logic. */
mx_status_t vcpu_loop(vcpu_context_t* context);

/* Processes a single guest packet. */
mx_status_t vcpu_handle_packet(vcpu_context_t* context, mx_guest_packet_t* packet);
