// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <hypervisor/block.h>
#include <hypervisor/vcpu.h>
#include <magenta/syscalls.h>
#include <virtio/block.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

#define VIRTIO_QUEUE_SIZE           128u

#define PCI_INTERRUPT_VIRTIO_BLOCK  33u
#define PCI_ALIGN(n)                ((((uintptr_t)n) + 4095) & ~4095)

static const virtio_blk_config_t block_config = {
    .capacity = (8u << 20) / SECTOR_SIZE,
    .size_max = 0,
    .seg_max = 0,
    .geometry = {
        .cylinders = 0,
        .heads = 0,
        .sectors = 0,
    },
    .blk_size = PAGE_SIZE,
};

mx_status_t handle_virtio_block_read(uint16_t port, uint8_t* input_size,
                                     mx_guest_port_in_ret_t* port_in_ret) {
    switch (port) {
    case VIRTIO_PCI_QUEUE_SIZE:
        *input_size = 2;
        port_in_ret->u16 = VIRTIO_QUEUE_SIZE;
        return MX_OK;
    case VIRTIO_PCI_DEVICE_STATUS:
        *input_size = 1;
        port_in_ret->u8 = 0;
        return MX_OK;
    case VIRTIO_PCI_ISR_STATUS:
        *input_size = 1;
        port_in_ret->u8 = 1;
        return MX_OK;
    case VIRTIO_PCI_CONFIG_OFFSET_NOMSI ...
         VIRTIO_PCI_CONFIG_OFFSET_NOMSI + sizeof(block_config) - 1: {
        *input_size = 1;
        uint8_t* buf = (uint8_t*)&block_config;
        port_in_ret->u8 = buf[port - VIRTIO_PCI_CONFIG_OFFSET_NOMSI];
        return MX_OK;
    }}

    fprintf(stderr, "Unhandled block read %#x\n", port);
    return MX_ERR_NOT_SUPPORTED;
}

// Returns the redirect interrupt associated with the global interrupt.
static uint8_t irq_redirect(const io_apic_state_t* io_apic_state, uint8_t global_irq) {
    return io_apic_state->redirect[global_irq * 2] & UINT8_MAX;
}

mx_status_t handle_virtio_block_write(vcpu_context_t* context, uint16_t port,
                                      const mx_guest_port_out_t* port_out) {
    void* mem_addr = context->guest_state->mem_addr;
    size_t mem_size = context->guest_state->mem_size;
    virtio_queue_t* queue = &context->guest_state->pci_device_state[PCI_DEVICE_VIRTIO_BLOCK].queue;
    switch (port) {
    case VIRTIO_PCI_DEVICE_STATUS:
        if (port_out->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        return MX_OK;
    case VIRTIO_PCI_QUEUE_PFN: {
        if (port_out->access_size != 4)
            return MX_ERR_IO_DATA_INTEGRITY;
        queue->desc = mem_addr + (port_out->u32 * PAGE_SIZE);
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
    case VIRTIO_PCI_QUEUE_SIZE:
        if (port_out->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        queue->size = port_out->u16;
        return MX_OK;
    case VIRTIO_PCI_QUEUE_SELECT:
        if (port_out->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        if (port_out->u16 != 0) {
            fprintf(stderr, "Only one queue per device is supported\n");
            return MX_ERR_NOT_SUPPORTED;
        }
        return MX_OK;
    case VIRTIO_PCI_QUEUE_NOTIFY: {
        if (port_out->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        if (port_out->u16 != 0) {
            fprintf(stderr, "Only one queue per device is supported\n");
            return MX_ERR_NOT_SUPPORTED;
        }
        mx_status_t status = null_block_device(mem_addr, mem_size, queue);
        if (status != MX_OK)
            return status;
        uint8_t interrupt = irq_redirect(&context->guest_state->io_apic_state,
                                         PCI_INTERRUPT_VIRTIO_BLOCK);
        return mx_hypervisor_op(context->guest, MX_HYPERVISOR_OP_GUEST_INTERRUPT,
                                &interrupt, sizeof(interrupt), NULL, 0);
    }}

    fprintf(stderr, "Unhandled block write %#x\n", port);
    return MX_ERR_NOT_SUPPORTED;
}

// Returns a circular index into a Virtio ring.
static uint32_t ring_index(virtio_queue_t* queue, uint32_t index) {
    return index % queue->size;
}

mx_status_t null_block_device(void* mem_addr, size_t mem_size, virtio_queue_t* queue) {
    for (; queue->index < queue->avail->idx; queue->index++, queue->used->idx++) {
        uint16_t desc_index = queue->avail->ring[ring_index(queue, queue->index)];
        if (desc_index >= queue->size)
            return MX_ERR_OUT_OF_RANGE;
        volatile struct vring_used_elem* used =
            &queue->used->ring[ring_index(queue, queue->used->idx)];
        used->id = desc_index;
        virtio_blk_req_t* req = NULL;
        while (true) {
            struct vring_desc desc = queue->desc[desc_index];
            const uint64_t end = desc.addr + desc.len;
            if (end < desc.addr || end > mem_size)
                return MX_ERR_OUT_OF_RANGE;
            if (req == NULL) {
                // Header.
                if (desc.len != sizeof(virtio_blk_req_t))
                    return MX_ERR_INVALID_ARGS;
                req = mem_addr + desc.addr;
            } else if (desc.flags & VRING_DESC_F_NEXT) {
                // Payload.
                if (req->type == VIRTIO_BLK_T_IN)
                    memset(mem_addr + desc.addr, 0, desc.len);
                used->len += desc.len;
            } else {
                // Status.
                if (desc.len != sizeof(uint8_t))
                    return MX_ERR_INVALID_ARGS;
                uint8_t* status = mem_addr + desc.addr;
                *status = VIRTIO_BLK_S_OK;
                break;
            }
            desc_index = desc.next;
        };
    }
    return MX_OK;
}
