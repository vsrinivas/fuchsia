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
#include <virtio/block.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ids.h>
#include <virtio/virtio_ring.h>

/* Block configuration constants. */
#define QUEUE_SIZE      128u

/* Interrupt vectors. */
#define X86_INT_BLOCK   33u

/* Get a pointer to a block_t from the underlying virtio device. */
static block_t* virtio_device_to_block(const virtio_device_t* virtio_device) {
    return (block_t*) virtio_device->impl;
}

static mx_status_t block_read(const virtio_device_t* device, uint16_t port,
                              mx_vcpu_io_t* vcpu_io) {
    block_t* block = virtio_device_to_block(device);
    virtio_blk_config_t config;
    memset(&config, 0, sizeof(virtio_blk_config_t));
    config.capacity = block->size / SECTOR_SIZE;
    config.blk_size = SECTOR_SIZE;

    uint8_t* buf = (uint8_t*)&config;
    vcpu_io->access_size = 1;
    vcpu_io->u8 = buf[port];
    return MX_OK;
}

static mx_status_t block_write(virtio_device_t* device, mx_handle_t vcpu,
                               uint16_t port, const mx_guest_io_t* io) {
    // No device fields are writable.
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t block_queue_notify(virtio_device_t* device, uint16_t queue_sel) {
    if (queue_sel != 0)
        return MX_ERR_INVALID_ARGS;
    return file_block_device(virtio_device_to_block(device));
}

static virtio_device_ops_t block_device_ops = {
    .read = &block_read,
    .write = &block_write,
    .queue_notify = &block_queue_notify,
};

static void block_pci_init(pci_device_t* pci_device) {
    pci_device->vendor_id = PCI_VENDOR_ID_VIRTIO;
    pci_device->device_id = PCI_DEVICE_ID_VIRTIO_BLOCK_LEGACY;
    pci_device->subsystem_vendor_id = 0;
    pci_device->subsystem_id = VIRTIO_ID_BLOCK;
    pci_device->class_code = PCI_CLASS_MASS_STORAGE;
    pci_device->bar_size = 0x40;
}

mx_status_t block_init(block_t* block, const char* path, void* guest_physmem_addr,
                       size_t guest_physmem_size, const io_apic_t* io_apic) {
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
                        "Block device will be read-only.\n", path);
        block->virtio_device.features |= VIRTIO_BLK_F_RO;
    }
    // Read file size.
    off_t ret = lseek(block->fd, 0, SEEK_END);
    if (ret < 0) {
        fprintf(stderr, "Failed to read size of block file \"%s\"\n", path);
        return MX_ERR_IO;
    }
    block->size = ret;

    // Setup Virtio device.
    block->virtio_device.global_irq = X86_INT_BLOCK;
    block->virtio_device.impl = block;
    block->virtio_device.num_queues = 1;
    block->virtio_device.queues = &block->queue;
    block->virtio_device.ops = &block_device_ops;
    block->virtio_device.guest_physmem_addr = guest_physmem_addr;
    block->virtio_device.guest_physmem_size = guest_physmem_size;
    block->virtio_device.io_apic = io_apic;

    // PCI Transport.
    block_pci_init(&block->virtio_device.pci_device);

    // Setup Virtio queue.
    block->queue.size = QUEUE_SIZE;
    block->queue.virtio_device = &block->virtio_device;

    return MX_OK;
}

static mx_status_t block_handler(void* ctx, mx_handle_t vcpu, mx_guest_packet_t* packet) {
    block_t* block = ctx;
    mx_guest_io_t* io = &packet->io;
    uint32_t bar_base = pci_bar_base(&block->virtio_device.pci_device);
    const uint16_t port_off = io->port - bar_base;
    return virtio_pci_legacy_write(&block->virtio_device, vcpu, port_off, io);
}

mx_status_t block_async(block_t* block, mx_handle_t vcpu, mx_handle_t guest, uint32_t bar_base,
                        uint16_t bar_size) {
    return device_async(vcpu, guest, MX_GUEST_TRAP_IO, bar_base, bar_size, block_handler, block);
}

// Multiple data buffers can be chained in the payload of block read/write
// requests. We pass along the offset (from the sector ID defined in the request
// header) so that subsequent requests can seek to the correct block location.
typedef struct file_state {
    block_t* block;
    off_t off;
} file_state_t;

static mx_status_t file_req(void* ctx, void* req, void* addr, uint32_t len) {
    file_state_t* state = ctx;
    block_t* block = state->block;
    virtio_blk_req_t* blk_req = req;

    off_t ret;
    if (blk_req->type != VIRTIO_BLK_T_FLUSH) {
        off_t off = blk_req->sector * SECTOR_SIZE + state->off;
        state->off += len;
        ret = lseek(block->fd, off, SEEK_SET);
        if (ret < 0)
            return MX_ERR_IO;
    }

    switch (blk_req->type) {
    case VIRTIO_BLK_T_IN:
        ret = read(block->fd, addr, len);
        break;
    case VIRTIO_BLK_T_OUT:
        // From VIRTIO Version 1.0: If the VIRTIO_BLK_F_RO feature is set by
        // the device, any write requests will fail.
        if (block->virtio_device.features & VIRTIO_BLK_F_RO)
            return MX_ERR_NOT_SUPPORTED;
        ret = write(block->fd, addr, len);
        break;
    case VIRTIO_BLK_T_FLUSH:
        // From VIRTIO Version 1.0: A driver MUST set sector to 0 for a
        // VIRTIO_BLK_T_FLUSH request. A driver SHOULD NOT include any data in a
        // VIRTIO_BLK_T_FLUSH request.
        if (blk_req->sector != 0)
            return MX_ERR_IO_DATA_INTEGRITY;
        len = 0;
        ret = fsync(block->fd);
        break;
    default:
        return MX_ERR_INVALID_ARGS;
    }
    return ret != len ? MX_ERR_IO : MX_OK;
}

mx_status_t file_block_device(block_t* block) {
    mx_status_t status;
    mtx_lock(&block->mutex);
    do {
        file_state_t state = { block, 0 };
        status = virtio_queue_handler(&block->queue, sizeof(virtio_blk_req_t), file_req, &state);
    } while (status == MX_ERR_NEXT);
    mtx_unlock(&block->mutex);
    return status;
}
