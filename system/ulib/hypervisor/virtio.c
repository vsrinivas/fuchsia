// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <hypervisor/io_apic.h>
#include <hypervisor/vcpu.h>
#include <hypervisor/virtio.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

/* PCI macros. */
#define PCI_ALIGN(n)    ((((uintptr_t)n) + 4095) & ~4095)

static virtio_queue_t* selected_queue(const virtio_device_t* device) {
    return device->queue_sel < device->num_queues ? &device->queues[device->queue_sel] : NULL;
}

mx_status_t virtio_pci_legacy_read(const virtio_device_t* device, uint16_t port,
                                   mx_vcpu_io_t* vcpu_io) {
    const virtio_queue_t* queue = selected_queue(device);
    switch (port) {
    case VIRTIO_PCI_DEVICE_FEATURES:
        vcpu_io->access_size = 4;
        vcpu_io->u32 = device->features;
        return MX_OK;
    case VIRTIO_PCI_QUEUE_PFN:
        if (!queue)
            return MX_ERR_NOT_SUPPORTED;
        vcpu_io->access_size = 4;
        vcpu_io->u32 = queue->pfn;
        return MX_OK;
    case VIRTIO_PCI_QUEUE_SIZE:
        if (!queue)
            return MX_ERR_NOT_SUPPORTED;
        vcpu_io->access_size = 2;
        vcpu_io->u16 = queue->size;
        return MX_OK;
    case VIRTIO_PCI_DEVICE_STATUS:
        vcpu_io->access_size = 1;
        vcpu_io->u8 = device->status;
        return MX_OK;
    case VIRTIO_PCI_ISR_STATUS:
        vcpu_io->access_size = 1;
        vcpu_io->u8 = 1;
        return MX_OK;
    }

    // Handle device-specific accesses.
    if (port >= VIRTIO_PCI_DEVICE_CFG_BASE) {
        uint16_t device_offset = port - VIRTIO_PCI_DEVICE_CFG_BASE;
        return device->ops->read(device, device_offset, vcpu_io);
    }

    fprintf(stderr, "Unhandled virtio device read %#x\n", port);
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t virtio_queue_set_pfn(virtio_queue_t* queue, uint32_t pfn) {
    void* mem_addr = queue->virtio_device->guest_physmem_addr;
    size_t mem_size = queue->virtio_device->guest_physmem_size;

    queue->pfn = pfn;
    queue->desc = mem_addr + (queue->pfn * PAGE_SIZE);
    queue->avail = (void*)&queue->desc[queue->size];
    queue->used_event = (void*)&queue->avail->ring[queue->size];
    queue->used = (void*)PCI_ALIGN(queue->used_event + sizeof(uint16_t));
    queue->avail_event = (void*)&queue->used->ring[queue->size];
    volatile const void* end = queue->avail_event + 1;
    if (end < (void*)queue->desc || end > mem_addr + mem_size) {
        fprintf(stderr, "Ring is outside of guest memory\n");
        memset(queue, 0, sizeof(virtio_queue_t));
        return MX_ERR_OUT_OF_RANGE;
    }

    return MX_OK;
}

mx_status_t virtio_pci_legacy_write(virtio_device_t* device, mx_handle_t vcpu,
                                    uint16_t port, const mx_guest_io_t* io) {
    virtio_queue_t* queue = selected_queue(device);
    switch (port) {
    case VIRTIO_PCI_DRIVER_FEATURES:
        if (io->access_size != 4)
            return MX_ERR_IO_DATA_INTEGRITY;
        // Currently we expect the driver to accept all our features.
        if (io->u32 != device->features)
            return MX_ERR_INVALID_ARGS;
        return MX_OK;
    case VIRTIO_PCI_DEVICE_STATUS:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        device->status = io->u8;
        return MX_OK;
    case VIRTIO_PCI_QUEUE_PFN: {
        if (io->access_size != 4)
            return MX_ERR_IO_DATA_INTEGRITY;
        if (!queue)
            return MX_ERR_NOT_SUPPORTED;
        return virtio_queue_set_pfn(queue, io->u32);
    }
    case VIRTIO_PCI_QUEUE_SIZE:
        if (io->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        queue->size = io->u16;
        return MX_OK;
    case VIRTIO_PCI_QUEUE_SELECT:
        if (io->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        if (io->u16 >= device->num_queues) {
            fprintf(stderr, "Selected queue does not exist.\n");
            return MX_ERR_NOT_SUPPORTED;
        }
        device->queue_sel = io->u16;
        return MX_OK;
    case VIRTIO_PCI_QUEUE_NOTIFY: {
        if (io->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        if (io->u16 >= device->num_queues) {
            fprintf(stderr, "Notify queue does not exist.\n");
            return MX_ERR_NOT_SUPPORTED;
        }
        mx_status_t status = device->ops->queue_notify(device, device->queue_sel);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to handle queue notify event. Error %d\n", status);
            return status;
        }
        uint32_t interrupt = io_apic_redirect(device->io_apic, device->irq_vector);
        return mx_vcpu_interrupt(vcpu, interrupt);
    }}

    // Handle device-specific accesses.
    if (port >= VIRTIO_PCI_DEVICE_CFG_BASE) {
        uint16_t device_offset = port - VIRTIO_PCI_DEVICE_CFG_BASE;
        return device->ops->write(device, vcpu, device_offset, io);
    }

    fprintf(stderr, "Unhandled virtio device write %#x\n", port);
    return MX_ERR_NOT_SUPPORTED;
}

// Returns a circular index into a Virtio ring.
static uint32_t ring_index(virtio_queue_t* queue, uint32_t index) {
    return index % queue->size;
}

static int ring_avail_count(virtio_queue_t* queue) {
    return queue->avail->idx - queue->index;
}

/* Map mx_status_t values to their Virtio counterparts. */
static uint8_t to_virtio_status(mx_status_t status) {
    switch (status) {
    case MX_OK:
        return VIRTIO_STATUS_OK;
    case MX_ERR_NOT_SUPPORTED:
        return VIRTIO_STATUS_UNSUPPORTED;
    default:
        return VIRTIO_STATUS_ERROR;
    }
}

mx_status_t virtio_queue_handler(virtio_queue_t* queue, uint32_t hdr_size,
                                 virtio_req_fn_t req_fn, void* ctx) {
    if (ring_avail_count(queue) < 1)
        return MX_OK;

    void* mem_addr = queue->virtio_device->guest_physmem_addr;
    size_t mem_size = queue->virtio_device->guest_physmem_size;
    uint16_t desc_index = queue->avail->ring[ring_index(queue, queue->index)];
    if (desc_index >= queue->size)
        return MX_ERR_OUT_OF_RANGE;
    volatile struct vring_used_elem* used =
        &queue->used->ring[ring_index(queue, queue->used->idx)];
    used->id = desc_index;

    void* req = NULL;
    bool has_payload = false;
    uint8_t req_status = VIRTIO_STATUS_OK;
    while (true) {
        struct vring_desc desc = queue->desc[desc_index];
        const uint64_t end = desc.addr + desc.len;
        if (end < desc.addr || end > mem_size)
            return MX_ERR_OUT_OF_RANGE;
        if (req == NULL) {
            // Header.
            if (desc.len != hdr_size)
                return MX_ERR_INVALID_ARGS;
            req = mem_addr + desc.addr;
        } else if (desc.flags & VRING_DESC_F_NEXT) {
            // Payload.
            has_payload = true;
            mx_status_t status = req_fn(ctx, req, mem_addr + desc.addr, desc.len);
            if (status != MX_OK) {
                fprintf(stderr, "Virtio request (%#lx, %u) failed %d\n",
                        desc.addr, desc.len, status);
                req_status = to_virtio_status(status);
            } else {
                used->len += desc.len;
            }
        } else {
            // Status.
            if (desc.len != sizeof(uint8_t))
                return MX_ERR_INVALID_ARGS;
            // If there was no payload, call the request function once.
            if (!has_payload) {
                mx_status_t status = req_fn(ctx, req, NULL, 0);
                if (status != MX_OK)
                    req_status = to_virtio_status(status);
            }
            uint8_t* virtio_status = mem_addr + desc.addr;
            *virtio_status = req_status;
            break;
        }
        desc_index = desc.next;
    }

    queue->index++;
    queue->used->idx++;
    return ring_avail_count(queue) > 0 ? MX_ERR_NEXT : MX_OK;
}
