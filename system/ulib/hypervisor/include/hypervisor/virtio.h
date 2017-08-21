// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hypervisor/pci.h>
#include <magenta/types.h>

// clang-format off

/* Accesses to this range are common accross device types. */
#define VIRTIO_PCI_COMMON_CFG_BASE      0
#define VIRTIO_PCI_COMMON_CFG_TOP       (VIRTIO_PCI_CONFIG_OFFSET_NOMSI - 1)

/* Accesses to this range are device specific. */
#define VIRTIO_PCI_DEVICE_CFG_BASE      VIRTIO_PCI_CONFIG_OFFSET_NOMSI
#define VIRTIO_PCI_DEVICE_CFG_TOP(size) (VIRTIO_PCI_DEVICE_CFG_BASE + size - 1)

// Interrupt status bits.
#define VIRTIO_ISR_QUEUE                0x1
#define VIRTIO_ISR_DEVICE               0x2

// clang-format on

struct vring_desc;
struct vring_avail;
struct vring_used;

typedef struct io_apic io_apic_t;
typedef struct mx_packet_guest_io mx_packet_guest_io_t;
typedef struct mx_vcpu_io mx_vcpu_io_t;
typedef struct virtio_device virtio_device_t;
typedef struct virtio_queue virtio_queue_t;

/* Device-specific operations. */
typedef struct virtio_device_ops {
    // Read a device configuration field.
    mx_status_t (*read)(const virtio_device_t* device, uint16_t port,
                        mx_vcpu_io_t* vcpu_io);

    // Write a device configuration field.
    mx_status_t (*write)(virtio_device_t* device, mx_handle_t vcpu, uint16_t port,
                         const mx_packet_guest_io_t* io);

    // Handle notify events for one of this devices queues.
    mx_status_t (*queue_notify)(virtio_device_t* device, uint16_t queue_sel);
} virtio_device_ops_t;

/* Common state shared by all virtio devices. */
typedef struct virtio_device {
    mtx_t mutex;
    // Virtio feature flags.
    uint32_t features;
    // Virtio device id.
    uint8_t device_id;
    // Virtio status register for the device.
    uint8_t status;
    // Interrupt status register.
    uint8_t isr_status;
    // Currently selected queue.
    uint16_t queue_sel;
    // Number of bytes used for this devices configuration space.
    //
    // This should cover only bytes used for the device-specific portions of
    // the configuration header, omitting any of the (transport-specific)
    // shared configuration space.
    uint32_t config_size;
    // Size of queues array.
    uint16_t num_queues;
    // Virtqueues for this device.
    virtio_queue_t* queues;

    // Address of guest physical memory.
    void* guest_physmem_addr;
    // Size of guest physical memory.
    size_t guest_physmem_size;

    // Device-specific operations.
    const virtio_device_ops_t* ops;
    // Private pointer for use by the device implementation.
    void* impl;
    // PCI device for the virtio-pci transport.
    pci_device_t pci_device;
} virtio_device_t;

/* Configures a device for Virtio PCI functionality.
 *
 * Should be invoked after the rest of the virtio_device_t structure has
 * already been intialized as the PCI configuration depends on some of the
 * virtio device attributes.
 */
void virtio_pci_init(virtio_device_t* device);

/* Send an interrupt back to the guest for a device. */
mx_status_t virtio_device_notify(virtio_device_t* device);

/* Stores the Virtio queue based on the ring provided by the guest.
 *
 * NOTE(abdulla): This structure points to guest-controlled memory.
 */
typedef struct virtio_queue {
    mtx_t mutex;

    // Queue PFN used to locate this queue in guest physical address space.
    uint32_t pfn;
    uint32_t size;
    uint16_t index;

    // Pointer to the owning device.
    virtio_device_t* virtio_device;

    volatile struct vring_desc* desc; // guest-controlled

    volatile struct vring_avail* avail; // guest-controlled
    volatile uint16_t* used_event; // guest-controlled

    volatile struct vring_used* used; // guest-controlled
    volatile uint16_t* avail_event; // guest-controlled
} virtio_queue_t;

/* Callback function for virtio_queue_handler.
 *
 * For chained buffers uing VRING_DESC_F_NEXT, this function will be called once for each buffer
 * in the chain.
 *
 * addr     - Pointer to the descriptor buffer.
 * len      - Length of the descriptor buffer.
 * flags    - Flags from the vring descriptor.
 * used     - To be incremented by the number of bytes used from addr.
 * ctx      - The same pointer passed to virtio_queue_handler.
 */
typedef mx_status_t (*virtio_queue_fn_t)(void* addr, uint32_t len, uint16_t flags, uint32_t* used,
                                         void* ctx);

/* Handles the next available descriptor in a Virtio queue, calling handler to
 * process individual payload buffers.
 *
 * On success the function either returns MX_OK if there are no more descriptors
 * available, or MX_ERR_NEXT if there are more available descriptors to process.
 */
mx_status_t virtio_queue_handler(virtio_queue_t* queue, virtio_queue_fn_t handler, void* ctx);

/* Get the index of the next descriptor in the available ring.
 *
 * If a buffer is a available, the descriptor index is written to |index|, the
 * queue index pointer is incremented, and MX_OK is returned.
 *
 * If no buffers are available MX_ERR_NOT_FOUND is returned.
 */
mx_status_t virtio_queue_next_avail(virtio_queue_t* queue, uint16_t* index);

/* Reads a single descriptor from the queue.
 *
 * This method should only be called using descriptor indicies acquired with
 * virtio_queue_next_avail (including any chained decriptors) and before
 * they've been released with virtio_queue_return.
 */
mx_status_t virtio_queue_read_desc(virtio_queue_t* queue, uint16_t index, void** addr,
                                   uint32_t* len, uint16_t* flags);


/* Return a descriptor to the used ring.
 *
 * |index| must be a value received from a call to virtio_queue_next_avail. Any
 * buffers accessed via |index| or any chained descriptors must not be used
 * after calling virtio_queue_return.
 */
void virtio_queue_return(virtio_queue_t* queue, uint16_t index, uint32_t len);
