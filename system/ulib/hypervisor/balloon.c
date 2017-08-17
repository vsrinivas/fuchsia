// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/balloon.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <hypervisor/vcpu.h>
#include <hypervisor/virtio.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <virtio/balloon.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ids.h>

#define QUEUE_SIZE 128u

/* PFNs into and out of the balloon are always 4k aligned. */
#define BALLOON_PAGE_SIZE 4096

static balloon_t* virtio_device_to_balloon(const virtio_device_t* device) {
    return (balloon_t*)device->impl;
}

static mx_status_t decommit_pages(balloon_t* balloon, uint64_t addr, uint64_t len) {
    return mx_vmo_op_range(balloon->vmo, MX_VMO_OP_DECOMMIT, addr, len, NULL, 0);
}

static mx_status_t commit_pages(balloon_t* balloon, uint64_t addr, uint64_t len) {
    return mx_vmo_op_range(balloon->vmo, MX_VMO_OP_COMMIT, addr, len, NULL, 0);
}

/* Structure passed to the inflate/deflate queue handler. */
typedef struct queue_ctx {
    balloon_t* balloon;
    // Operation to perform on the queue (inflate or deflate).
    mx_status_t (*op)(balloon_t* balloon, uint64_t addr, uint64_t len);
} queue_ctx_t;

/* Handle balloon inflate/deflate requests.
 *
 * From VIRTIO 1.0 Section 5.5.6:
 *
 * To supply memory to the balloon (aka. inflate):
 *  (a) The driver constructs an array of addresses of unused memory pages.
 *      These addresses are divided by 4096 and the descriptor describing the
 *      resulting 32-bit array is added to the inflateq.
 *
 * To remove memory from the balloon (aka. deflate):
 *  (a) The driver constructs an array of addresses of memory pages it has
 *      previously given to the balloon, as described above. This descriptor is
 *      added to the deflateq.
 *  (b) If the VIRTIO_BALLOON_F_MUST_TELL_HOST feature is negotiated, the guest
 *      informs the device of pages before it uses them.
 *  (c) Otherwise, the guest is allowed to re-use pages previously given to the
 *      balloon before the device has acknowledged their withdrawal.
 */
static mx_status_t queue_range_op(void* addr, uint32_t len, uint16_t flags, uint32_t* used,
                                  void* ctx) {
    queue_ctx_t* balloon_op_ctx = ctx;
    balloon_t* balloon = balloon_op_ctx->balloon;
    uint32_t* pfns = addr;
    uint32_t pfn_count = len / 4;

    // If the driver writes contiguous PFNs to the array we'll batch them up
    // when invoking the inflate/deflate operation.
    uint64_t region_base = 0;
    uint64_t region_length = 0;
    for (uint32_t i = 0; i < pfn_count; ++i) {
        // If we have a contiguous page, increment the length & continue.
        if (region_length > 0 && (region_base + region_length) == pfns[i]) {
            region_length++;
            continue;
        }

        // If we have an existing region; invoke the inflate/deflate op.
        if (region_length > 0) {
            mx_status_t status = balloon_op_ctx->op(balloon, region_base * BALLOON_PAGE_SIZE,
                                                    region_length * BALLOON_PAGE_SIZE);
            if (status != MX_OK)
                return status;
        }

        // Create a new region.
        region_base = pfns[i];
        region_length = 1;
    }

    // Handle the last region.
    if (region_length > 0) {
        mx_status_t status = balloon_op_ctx->op(balloon, region_base * BALLOON_PAGE_SIZE,
                                                region_length * BALLOON_PAGE_SIZE);
        if (status != MX_OK)
            return status;
    }

    return MX_OK;
}

static mx_status_t handle_queue_notify(balloon_t* balloon, uint16_t queue_sel) {
    queue_ctx_t ctx;
    switch (queue_sel) {
    case VIRTIO_BALLOON_Q_INFLATEQ:
        ctx.op = &decommit_pages;
        break;
    case VIRTIO_BALLOON_Q_DEFLATEQ:
        ctx.op = &commit_pages;
        break;
    default:
        return MX_ERR_INVALID_ARGS;
    }
    ctx.balloon = balloon;
    return virtio_queue_handler(&balloon->queues[queue_sel], &queue_range_op, &ctx);
}

static mx_status_t balloon_queue_notify(virtio_device_t* device, uint16_t queue_sel) {
    mx_status_t status;
    balloon_t* balloon = virtio_device_to_balloon(device);
    do {
        status = handle_queue_notify(balloon, queue_sel);
    } while (status == MX_ERR_NEXT);
    return status;
}

static mx_status_t balloon_read(const virtio_device_t* device, uint16_t port,
                                mx_vcpu_io_t* vcpu_io) {
    balloon_t* balloon = virtio_device_to_balloon(device);
    uint8_t* buf = (uint8_t*)&balloon->config;
    vcpu_io->access_size = 1;
    vcpu_io->u8 = buf[port];
    return MX_OK;
}

static mx_status_t balloon_write(virtio_device_t* device, mx_handle_t vcpu,
                                 uint16_t port, const mx_guest_io_t* io) {
    if (io->access_size != 1)
        return MX_ERR_NOT_SUPPORTED;

    balloon_t* balloon = virtio_device_to_balloon(device);
    uint8_t* buf = (uint8_t*)&balloon->config;
    buf[port] = io->u8;
    return MX_OK;
}

static const virtio_device_ops_t kBalloonVirtioDeviceOps = {
    .read = &balloon_read,
    .write = &balloon_write,
    .queue_notify = &balloon_queue_notify,
};

void balloon_init(balloon_t* balloon, void* guest_physmem_addr, size_t guest_physmem_size,
                  mx_handle_t guest_physmem_vmo) {
    memset(balloon, 0, sizeof(*balloon));

    // Virt queue initialization.
    for (int i = 0; i < VIRTIO_BALLOON_Q_COUNT; ++i) {
        virtio_queue_t* queue = &balloon->queues[i];
        queue->size = QUEUE_SIZE;
        queue->virtio_device = &balloon->virtio_device;
    }

    // Setup virtio device.
    balloon->virtio_device.device_id = VIRTIO_ID_BALLOON;
    balloon->virtio_device.config_size = sizeof(virtio_balloon_config_t);
    balloon->virtio_device.impl = balloon;
    balloon->virtio_device.num_queues = VIRTIO_BALLOON_Q_COUNT;
    balloon->virtio_device.queues = balloon->queues;
    balloon->virtio_device.ops = &kBalloonVirtioDeviceOps;
    balloon->virtio_device.guest_physmem_addr = guest_physmem_addr;
    balloon->virtio_device.guest_physmem_size = guest_physmem_size;

    // Device configuration values.
    balloon->vmo = guest_physmem_vmo;
    balloon->config.num_pages = 0;
    balloon->config.actual = 0;

    // PCI Transport.
    virtio_pci_init(&balloon->virtio_device);
}
