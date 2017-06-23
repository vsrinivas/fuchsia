// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <magenta/types.h>
#include <virtio/virtio_ring.h>

#define IO_APIC_REDIRECT_OFFSETS    128u
#define IO_BUFFER_SIZE              512u
#define PCI_MAX_DEVICES             2u
#define PCI_MAX_BARS                1u
#define VIRTIO_QUEUE_SIZE           128u

/* Stores the local APIC state across VM exits. */
typedef struct local_apic_state {
    // Address of the local APIC.
    void* apic_addr;
    // VMO representing the local APIC.
    mx_handle_t apic_mem;
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
    // Buffer containing the output.
    uint8_t buffer[IO_BUFFER_SIZE];
    // Write position within the buffer.
    uint16_t offset;
    // Index of the RTC register to use.
    uint8_t rtc_index;
    // Command being issued to the i8042 controller.
    uint8_t i8042_command;
    // State of power management enable register.
    uint16_t pm1_enable;
    // State of the UART line control register.
    uint8_t uart_line_control;
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
    // Virtio queue for the device.
    virtio_queue_t queue;
} pci_device_state_t;

typedef struct guest_state {
    mtx_t mutex;

    // Guest memory.
    void* mem_addr;
    size_t mem_size;

    io_apic_state_t io_apic_state;
    io_port_state_t io_port_state;
    pci_device_state_t pci_device_state[PCI_MAX_DEVICES];
} guest_state_t;

typedef struct vcpu_context {
    mx_handle_t guest;
    mx_handle_t vcpu_fifo;

    local_apic_state_t local_apic_state;
    guest_state_t* guest_state;
} vcpu_context_t;

mx_status_t vcpu_loop(vcpu_context_t* context);
