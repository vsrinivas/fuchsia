// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <hypervisor/pci.h>
#include <hypervisor/virtio.h>
#include <magenta/syscalls/hypervisor.h>
#include <magenta/types.h>

#define IO_APIC_REDIRECT_OFFSETS    128u

typedef struct uart_state uart_state_t;

/* Stores the IO APIC state. */
typedef struct io_apic_state {
    // IO register-select register.
    uint32_t select;
    // IO APIC identification register.
    uint32_t id;
    // IO redirection table offsets.
    uint32_t redirect[IO_APIC_REDIRECT_OFFSETS];
} io_apic_state_t;

/* Stores the IO port state. */
typedef struct io_port_state {
    // Index of the RTC register to use.
    uint8_t rtc_index;
    // Command being issued to the i8042 controller.
    uint8_t i8042_command;
    // State of power management enable register.
    uint16_t pm1_enable;
    // Selected address in PCI config space.
    uint32_t pci_config_address;
} io_port_state_t;

typedef struct guest_state {
    mx_handle_t guest;
    mtx_t mutex;

    // Guest memory.
    void* mem_addr;
    size_t mem_size;

    // Guest block.
    int block_fd;
    // Virtio status register.
    uint8_t block_status;
    uint64_t block_size;
    virtio_queue_t block_queue;

    uart_state_t* uart_state;

    io_apic_state_t io_apic_state;
    io_port_state_t io_port_state;
    pci_device_state_t pci_device_state[PCI_MAX_DEVICES];
} guest_state_t;

/* Stores the local APIC state. */
typedef struct local_apic_state {
    // Address of the local APIC.
    void* apic_addr;
} local_apic_state_t;

/* Typedefs to abstract reading and writing VCPU state. */
typedef struct vcpu_context vcpu_context_t;
typedef mx_status_t (*read_state_fn_t)
    (vcpu_context_t* vcpu, uint32_t kind, void* buffer, uint32_t len);
typedef mx_status_t (*write_state_fn_t)
    (vcpu_context_t* vcpu, uint32_t kind, const void* buffer, uint32_t len);

/* Stores the state associated with a single VCPU. */
typedef struct vcpu_context {
    mx_handle_t vcpu;
    local_apic_state_t local_apic_state;
    guest_state_t* guest_state;

    // Accessors to the VCPU registers. These are initialized in vcpu_init.
    read_state_fn_t read_state;
    write_state_fn_t write_state;
} vcpu_context_t;

/* Initializes a VCPU context. */
void vcpu_init(vcpu_context_t* context);

/* Controls execution of a VCPU context, providing the main logic. */
mx_status_t vcpu_loop(vcpu_context_t* context);

/* Processes a single guest packet. */
mx_status_t vcpu_handle_packet(vcpu_context_t* context, mx_guest_packet_t* packet);

/* Returns the redirected IRQ for the given global one. */
uint8_t irq_redirect(const io_apic_state_t* io_apic_state, uint8_t global_irq);
