// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <magenta/syscalls/hypervisor.h>
#include <magenta/types.h>

#define IO_APIC_REDIRECT_OFFSETS    128u

#define PCI_DEVICE_ROOT_COMPLEX     0u
#define PCI_DEVICE_VIRTIO_BLOCK     1u
#define PCI_MAX_DEVICES             2u
#define PCI_MAX_BARS                1u

struct vring_desc;
struct vring_avail;
struct vring_used;

/* Stores the local APIC state across VM exits. */
typedef struct local_apic_state {
    // Address of the local APIC.
    void* apic_addr;
} local_apic_state_t;

/* Stores the IO APIC state across VM exits. */
typedef struct io_apic_state {
    // IO register-select register.
    uint32_t select;
    // IO APIC identification register.
    uint32_t id;
    // IO redirection table offsets.
    uint32_t redirect[IO_APIC_REDIRECT_OFFSETS];
} io_apic_state_t;

/* Stores the IO port state across VM exits. */
typedef struct io_port_state {
    // Index of the RTC register to use.
    uint8_t rtc_index;
    // Command being issued to the i8042 controller.
    uint8_t i8042_command;
    // State of power management enable register.
    uint16_t pm1_enable;
    // State of the UART line control register.
    uint8_t uart_line_control;
    // Selected address in PCI config space.
    uint32_t pci_config_address;
} io_port_state_t;

/* Stores the Virtio queue based on the ring provided by the guest.
 *
 * NOTE(abdulla): This structure points to guest-controlled memory.
 */
typedef struct virtio_queue {
    uint32_t size;
    uint16_t index;
    volatile struct vring_desc* desc;   // guest-controlled

    volatile struct vring_avail* avail; // guest-controlled
    volatile uint16_t* used_event;      // guest-controlled

    volatile struct vring_used* used;   // guest-controlled
    volatile uint16_t* avail_event;     // guest-controlled
} virtio_queue_t;

/* Stores the state of PCI devices across VM exists. */
typedef struct pci_device_state {
    // Command register.
    uint16_t command;
    // Base address registers.
    uint32_t bar[PCI_MAX_BARS];
} pci_device_state_t;

typedef struct guest_state {
    mx_handle_t guest;
    mtx_t mutex;

    // Guest memory.
    void* mem_addr;
    size_t mem_size;

    // Guest block.
    int block_fd;
    uint64_t block_size;
    virtio_queue_t block_queue;

    io_apic_state_t io_apic_state;
    io_port_state_t io_port_state;
    pci_device_state_t pci_device_state[PCI_MAX_DEVICES];
} guest_state_t;

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

/* Processes a single UART IO packet. */
mx_status_t vcpu_handle_uart(mx_guest_io_t* io, mtx_t* mutex, io_port_state_t* io_port_state);
