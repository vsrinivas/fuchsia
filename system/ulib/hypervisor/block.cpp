// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <hypervisor/block.h>
#include <hypervisor/io_apic.h>
#include <hypervisor/pci.h>
#include <hypervisor/vcpu.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ids.h>
#include <virtio/virtio_ring.h>

#include "virtio_priv.h"

/* Block configuration constants. */
#define QUEUE_SIZE 128u

/* Get a pointer to a block_t from the underlying virtio device. */
static block_t* virtio_device_to_block(const virtio_device_t* virtio_device) {
    return (block_t*)virtio_device->impl;
}

static mx_status_t block_read(const virtio_device_t* device, uint16_t port, uint8_t access_size,
                              mx_vcpu_io_t* vcpu_io) {
    block_t* block = virtio_device_to_block(device);
    return virtio_device_config_read(device, &block->config, port, access_size, vcpu_io);
}

static mx_status_t block_write(virtio_device_t* device, uint16_t port,
                               const mx_vcpu_io_t* io) {
    // No device fields are writable.
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t block_queue_notify(virtio_device_t* device, uint16_t queue_sel) {
    if (queue_sel != 0)
        return MX_ERR_INVALID_ARGS;
    return file_block_device(virtio_device_to_block(device));
}

static const virtio_device_ops_t kBlockVirtioDeviceOps = {
    .read = &block_read,
    .write = &block_write,
    .queue_notify = &block_queue_notify,
};

mx_status_t block_init(block_t* block, const char* path, uintptr_t guest_physmem_addr,
                       size_t guest_physmem_size) {
    memset(block, 0, sizeof(*block));

    // Open block file. First try to open as read-write but fall back to read
    // only if that fails.
    block->fd = open(path, O_RDWR);
    if (block->fd < 0) {
        block->fd = open(path, O_RDONLY);
        if (block->fd < 0) {
            fprintf(stderr, "Failed to open block file \"%s\"\n", path);
            return MX_ERR_IO;
        }
        fprintf(stderr, "Unable to open block file \"%s\" read-write. "
                        "Block device will be read-only.\n",
                path);
        block->virtio_device.features |= VIRTIO_BLK_F_RO;
    }
    // Read file size.
    off_t ret = lseek(block->fd, 0, SEEK_END);
    if (ret < 0) {
        fprintf(stderr, "Failed to read size of block file \"%s\"\n", path);
        return MX_ERR_IO;
    }
    block->size = ret;
    block->config.capacity = block->size / SECTOR_SIZE;

    // Setup Virtio device.
    block->virtio_device.device_id = VIRTIO_ID_BLOCK;
    block->virtio_device.config_size = sizeof(virtio_blk_config_t);
    block->virtio_device.impl = block;
    block->virtio_device.num_queues = 1;
    block->virtio_device.queues = &block->queue;
    block->virtio_device.ops = &kBlockVirtioDeviceOps;
    block->virtio_device.guest_physmem_addr = guest_physmem_addr;
    block->virtio_device.guest_physmem_size = guest_physmem_size;
    // Virtio 1.0: 5.2.5.2: Devices SHOULD always offer VIRTIO_BLK_F_FLUSH
    block->virtio_device.features |= VIRTIO_BLK_F_FLUSH
                                     // Required by magenta guests.
                                     | VIRTIO_BLK_F_BLK_SIZE;
    block->config.blk_size = SECTOR_SIZE;

    // Setup Virtio queue.
    block->queue.size = QUEUE_SIZE;
    block->queue.virtio_device = &block->virtio_device;

    // PCI Transport.
    virtio_pci_init(&block->virtio_device);

    return MX_OK;
}

// Multiple data buffers can be chained in the payload of block read/write
// requests. We pass along the offset (from the sector ID defined in the request
// header) so that subsequent requests can seek to the correct block location.
typedef struct file_state {
    block_t* block;
    off_t off;

    bool has_payload;
    virtio_blk_req_t* blk_req;
    uint8_t status;
} file_state_t;

static mx_status_t file_req(file_state_t* state, void* addr, uint32_t len) {
    block_t* block = state->block;
    virtio_blk_req_t* blk_req = state->blk_req;

    // From VIRTIO Version 1.0: If the VIRTIO_BLK_F_RO feature is set by
    // the device, any write requests will fail.
    if (blk_req->type == VIRTIO_BLK_T_OUT && (block->virtio_device.features & VIRTIO_BLK_F_RO))
        return MX_ERR_NOT_SUPPORTED;

    // From VIRTIO Version 1.0: A driver MUST set sector to 0 for a
    // VIRTIO_BLK_T_FLUSH request. A driver SHOULD NOT include any data in a
    // VIRTIO_BLK_T_FLUSH request.
    if (blk_req->type == VIRTIO_BLK_T_FLUSH && blk_req->sector != 0)
        return MX_ERR_IO_DATA_INTEGRITY;

    mtx_lock(&block->file_mutex);
    off_t ret;
    if (blk_req->type != VIRTIO_BLK_T_FLUSH) {
        off_t off = blk_req->sector * SECTOR_SIZE + state->off;
        state->off += len;
        ret = lseek(block->fd, off, SEEK_SET);
        if (ret < 0) {
            mtx_unlock(&block->file_mutex);
            return MX_ERR_IO;
        }
    }

    switch (blk_req->type) {
    case VIRTIO_BLK_T_IN:
        ret = read(block->fd, addr, len);
        break;
    case VIRTIO_BLK_T_OUT:
        ret = write(block->fd, addr, len);
        break;
    case VIRTIO_BLK_T_FLUSH:
        len = 0;
        ret = fsync(block->fd);
        break;
    default:
        mtx_unlock(&block->file_mutex);
        return MX_ERR_INVALID_ARGS;
    }
    mtx_unlock(&block->file_mutex);
    return ret != len ? MX_ERR_IO : MX_OK;
}

/* Map mx_status_t values to their Virtio counterparts. */
static uint8_t to_virtio_status(mx_status_t status) {
    switch (status) {
    case MX_OK:
        return VIRTIO_BLK_S_OK;
    case MX_ERR_NOT_SUPPORTED:
        return VIRTIO_BLK_S_UNSUPP;
    default:
        return VIRTIO_BLK_S_IOERR;
    }
}

static mx_status_t block_queue_handler(void* addr, uint32_t len, uint16_t flags, uint32_t* used,
                                       void* context) {
    file_state_t* file_state = (file_state_t*)context;

    // Header.
    if (file_state->blk_req == NULL) {
        if (len != sizeof(*file_state->blk_req))
            return MX_ERR_INVALID_ARGS;
        file_state->blk_req = static_cast<virtio_blk_req_t*>(addr);
        return MX_OK;
    }

    // Payload.
    if (flags & VRING_DESC_F_NEXT) {
        file_state->has_payload = true;
        mx_status_t status = file_req(file_state, addr, len);
        if (status != MX_OK) {
            file_state->status = to_virtio_status(status);
        } else {
            *used += len;
        }
        return MX_OK;
    }

    // Status.
    if (len != sizeof(uint8_t))
        return MX_ERR_INVALID_ARGS;

    // If there was no payload, call the handler function once.
    if (!file_state->has_payload) {
        mx_status_t status = file_req(file_state, addr, len);
        if (status != MX_OK)
            file_state->status = to_virtio_status(status);
    }
    uint8_t* status = static_cast<uint8_t*>(addr);
    *status = file_state->status;
    return MX_OK;
}

mx_status_t file_block_device(block_t* block) {
    mx_status_t status;
    do {
        file_state_t state = {
            .block = block,
            .off = 0,
            .has_payload = false,
            .blk_req = NULL,
            .status = VIRTIO_BLK_S_OK,
        };
        status = virtio_queue_handler(&block->queue, &block_queue_handler, &state);
    } while (status == MX_ERR_NEXT);
    return status;
}
