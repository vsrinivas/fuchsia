// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <hypervisor/block.h>
#include <hypervisor/vcpu.h>
#include <magenta/syscalls.h>
#include <virtio/block.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ring.h>

#define VIRTIO_QUEUE_SIZE           128u

#define PCI_INTERRUPT_VIRTIO_BLOCK  33u
#define PCI_ALIGN(n)                ((((uintptr_t)n) + 4095) & ~4095)

typedef mx_status_t (* virtio_req_fn)(void* ctx, void* req, void* addr, uint32_t len);

mx_status_t handle_virtio_block_read(guest_state_t* guest_state, uint16_t port, uint8_t* input_size,
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
         VIRTIO_PCI_CONFIG_OFFSET_NOMSI + sizeof(virtio_blk_config_t) - 1: {
        virtio_blk_config_t config;
        memset(&config, 0, sizeof(virtio_blk_config_t));
        config.capacity = guest_state->block_size / SECTOR_SIZE;
        config.blk_size = PAGE_SIZE;

        *input_size = 1;
        uint8_t* buf = (uint8_t*)&config;
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
    int block_fd = context->guest_state->block_fd;
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
        mx_status_t status;
        if (block_fd < 0) {
            status = null_block_device(queue, mem_addr, mem_size);
        } else {
            status = file_block_device(queue, mem_addr, mem_size, block_fd);
        }
        if (status != MX_OK) {
            fprintf(stderr, "Block device operation failed %d\n", status);
            return status;
        }
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

static mx_status_t handle_virtio_queue(virtio_queue_t* queue, void* mem_addr, size_t mem_size,
                                       uint32_t hdr_size, void* ctx, virtio_req_fn req_fn) {
    for (; queue->index < queue->avail->idx; queue->index++, queue->used->idx++) {
        uint16_t desc_index = queue->avail->ring[ring_index(queue, queue->index)];
        if (desc_index >= queue->size)
            return MX_ERR_OUT_OF_RANGE;
        volatile struct vring_used_elem* used =
            &queue->used->ring[ring_index(queue, queue->used->idx)];
        used->id = desc_index;

        void* req = NULL;
        bool has_payload = false;
        uint8_t blk_status = VIRTIO_BLK_S_OK;
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
                    blk_status = VIRTIO_BLK_S_IOERR;
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
                        blk_status = VIRTIO_BLK_S_IOERR;
                }
                uint8_t* virtio_status = mem_addr + desc.addr;
                *virtio_status = blk_status;
                break;
            }
            desc_index = desc.next;
        };
    }
    return MX_OK;
}

mx_status_t null_req(void* ctx, void* req, void* addr, uint32_t len) {
    virtio_blk_req_t* blk_req = req;
    switch (blk_req->type) {
    case VIRTIO_BLK_T_IN:
        memset(addr, 0, len);
        /* fallthrough */
    case VIRTIO_BLK_T_OUT:
        return MX_OK;
    case VIRTIO_BLK_T_FLUSH:
        // See note in file_req.
        if (blk_req->sector != 0)
            return MX_ERR_IO_DATA_INTEGRITY;
        return MX_OK;
    }
    return MX_ERR_INVALID_ARGS;
}

mx_status_t null_block_device(virtio_queue_t* queue, void* mem_addr, size_t mem_size) {
    return handle_virtio_queue(queue, mem_addr, mem_size, sizeof(virtio_blk_req_t), NULL, null_req);
}

typedef struct file_state {
    int fd;
    off_t off;
} file_state_t;

mx_status_t file_req(void* ctx, void* req, void* addr, uint32_t len) {
    file_state_t* state = ctx;
    virtio_blk_req_t* blk_req = req;

    off_t ret;
    if (blk_req->type != VIRTIO_BLK_T_FLUSH) {
        off_t off = blk_req->sector * SECTOR_SIZE + state->off;
        state->off += len;
        ret = lseek(state->fd, off, SEEK_SET);
        if (ret < 0)
            return MX_ERR_IO;
    }

    switch (blk_req->type) {
    case VIRTIO_BLK_T_IN:
        ret = read(state->fd, addr, len);
        break;
    case VIRTIO_BLK_T_OUT:
        ret = write(state->fd, addr, len);
        break;
    case VIRTIO_BLK_T_FLUSH:
        // From VIRTIO Version 1.0: A driver MUST set sector to 0 for a
        // VIRTIO_BLK_T_FLUSH request. A driver SHOULD NOT include any data in a
        // VIRTIO_BLK_T_FLUSH request.
        if (blk_req->sector != 0)
            return MX_ERR_IO_DATA_INTEGRITY;
        len = 0;
        ret = fsync(state->fd);
        break;
    default:
        return MX_ERR_INVALID_ARGS;
    }
    return ret != len ? MX_ERR_IO : MX_OK;
}

mx_status_t file_block_device(virtio_queue_t* queue, void* mem_addr, size_t mem_size, int fd) {
    file_state_t state = { fd, 0 };
    return handle_virtio_queue(queue, mem_addr, mem_size, sizeof(virtio_blk_req_t), &state, file_req);
}
