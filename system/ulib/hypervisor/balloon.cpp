// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/balloon.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <fbl/auto_lock.h>
#include <hypervisor/vcpu.h>
#include <hypervisor/virtio.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>
#include <virtio/balloon.h>
#include <virtio/virtio.h>
#include <virtio/virtio_ids.h>

#include "virtio_priv.h"

#define QUEUE_SIZE 128u

static balloon_t* virtio_device_to_balloon(const virtio_device_t* device) {
    return (balloon_t*)device->impl;
}

static mx_status_t decommit_pages(balloon_t* balloon, uint64_t addr, uint64_t len) {
    return mx_vmo_op_range(balloon->vmo, MX_VMO_OP_DECOMMIT, addr, len, NULL, 0);
}

static mx_status_t commit_pages(balloon_t* balloon, uint64_t addr, uint64_t len) {
    if (balloon->deflate_on_demand)
        return MX_OK;
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
    queue_ctx_t* balloon_op_ctx = static_cast<queue_ctx_t*>(ctx);
    balloon_t* balloon = balloon_op_ctx->balloon;
    uint32_t* pfns = static_cast<uint32_t*>(addr);
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
            mx_status_t status = balloon_op_ctx->op(balloon,
                                                    region_base * VIRTIO_BALLOON_PAGE_SIZE,
                                                    region_length * VIRTIO_BALLOON_PAGE_SIZE);
            if (status != MX_OK)
                return status;
        }

        // Create a new region.
        region_base = pfns[i];
        region_length = 1;
    }

    // Handle the last region.
    if (region_length > 0) {
        mx_status_t status = balloon_op_ctx->op(balloon, region_base * VIRTIO_BALLOON_PAGE_SIZE,
                                                region_length * VIRTIO_BALLOON_PAGE_SIZE);
        if (status != MX_OK)
            return status;
    }

    return MX_OK;
}

static mx_status_t handle_queue_notify(balloon_t* balloon, uint16_t queue_sel) {
    queue_ctx_t ctx;
    switch (queue_sel) {
    case VIRTIO_BALLOON_Q_STATSQ:
        return MX_OK;
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

static mx_status_t balloon_read(const virtio_device_t* device, uint16_t port, uint8_t access_size,
                                mx_vcpu_io_t* vcpu_io) {
    balloon_t* balloon = virtio_device_to_balloon(device);

    fbl::AutoLock lock(&balloon->mutex);
    return virtio_device_config_read(device, &balloon->config, port, access_size, vcpu_io);
}

static mx_status_t balloon_write(virtio_device_t* device, uint16_t port,
                                 const mx_vcpu_io_t* io) {
    balloon_t* balloon = virtio_device_to_balloon(device);

    fbl::AutoLock lock(&balloon->mutex);
    return virtio_device_config_write(device, &balloon->config, port, io);
}

static const virtio_device_ops_t kBalloonVirtioDeviceOps = {
    .read = &balloon_read,
    .write = &balloon_write,
    .queue_notify = &balloon_queue_notify,
};

void balloon_init(balloon_t* balloon, uintptr_t guest_physmem_addr, size_t guest_physmem_size,
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
    balloon->virtio_device.features = VIRTIO_BALLOON_F_STATS_VQ | VIRTIO_BALLOON_F_DEFLATE_ON_OOM;

    // Device configuration values.
    balloon->vmo = guest_physmem_vmo;
    balloon->config.num_pages = 0;
    balloon->config.actual = 0;

    // PCI Transport.
    virtio_pci_init(&balloon->virtio_device);
}

static void wait_for_stats_buffer(balloon_t* balloon, virtio_queue_t* stats_queue) {
    if (!balloon->stats.has_buffer) {
        virtio_queue_wait(stats_queue, &balloon->stats.desc_index);
        balloon->stats.has_buffer = true;
    }
}

mx_status_t balloon_request_stats(balloon_t* balloon, balloon_stats_fn_t handler, void* ctx) {
    mx_status_t status;
    virtio_queue_t* stats_queue = &balloon->queues[VIRTIO_BALLOON_Q_STATSQ];

    // stats.mutex needs to be held during the entire time the guest is
    // processing the buffer since we need to make sure no other threads
    // can grab the returned stats buffer before we process it.
    mtx_lock(&balloon->stats.mutex);

    // We need an initial buffer we can return to return to the device to
    // request stats from the device. This should be immediately available in
    // the common case but we can race the driver for the initial buffer.
    wait_for_stats_buffer(balloon, stats_queue);

    // We have a buffer. We need to return it to the driver. It'll populate
    // a new buffer with stats and then send it back to us.
    balloon->stats.has_buffer = false;
    virtio_queue_return(stats_queue, balloon->stats.desc_index, 0);
    status = virtio_device_notify(&balloon->virtio_device);
    if (status != MX_OK) {
        mtx_unlock(&balloon->stats.mutex);
        return status;
    }
    wait_for_stats_buffer(balloon, stats_queue);

    virtio_desc_t desc;
    status = virtio_queue_read_desc(stats_queue, balloon->stats.desc_index, &desc);
    if (status != MX_OK) {
        mtx_unlock(&balloon->stats.mutex);
        return status;
    }

    if ((desc.len % sizeof(virtio_balloon_stat_t)) != 0) {
        mtx_unlock(&balloon->stats.mutex);
        return MX_ERR_IO_DATA_INTEGRITY;
    }

    // Invoke the handler on the stats.
    auto stats = static_cast<const virtio_balloon_stat_t*>(desc.addr);
    size_t stats_count = desc.len / sizeof(virtio_balloon_stat_t);
    handler(stats, stats_count, ctx);
    mtx_unlock(&balloon->stats.mutex);

    // Note we deliberately do not return the buffer here. This will be done to
    // initiate the next stats request.
    return MX_OK;
}

mx_status_t balloon_update_num_pages(balloon_t* balloon, uint32_t num_pages) {
    mtx_lock(&balloon->mutex);
    balloon->config.num_pages = num_pages;
    mtx_unlock(&balloon->mutex);

    // Send a config change interrupt to the guest.
    virtio_device_t* virtio_device = &balloon->virtio_device;
    mtx_lock(&virtio_device->mutex);
    virtio_device->isr_status |= VIRTIO_ISR_DEVICE;
    mtx_unlock(&virtio_device->mutex);
    return virtio_device_notify(virtio_device);
}
