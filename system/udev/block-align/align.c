// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <magenta/listnode.h>
#include <sys/param.h>
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <threads.h>

// This block device aligns all incoming requests to the size of the underlying
// block device, giving the underlying block device the appearance of a regular
// file.

typedef struct align_device {
    mx_device_t device;
    uint64_t blksize;
} align_device_t;

#define get_aligned_device(dev) containerof(dev, align_device_t, device)

// implement device protocol:

static ssize_t align_ioctl(mx_device_t* dev, uint32_t op, const void* cmd,
                           size_t cmdlen, void* reply, size_t max) {
    mx_device_t* parent = dev->parent;
    return parent->ops->ioctl(parent, op, cmd, cmdlen, reply, max);
}

static void aligned_write_complete(iotxn_t* txn_aligned, void* cookie) {
    iotxn_t* txn = cookie;
    mx_status_t status = txn_aligned->status;
    txn_aligned->ops->release(txn_aligned);
    txn->ops->complete(txn, status, txn->length);
}

static void aligned_read_complete(iotxn_t* txn_aligned, void* cookie) {
    iotxn_t* txn = cookie;
    mx_status_t status = txn_aligned->status;
    mx_off_t actual = 0;
    if (status != NO_ERROR) {
        goto done;
    } else if (txn->opcode == IOTXN_OP_READ) {
        // Copy the result from the aligned read into the original txn
        void* buffer;
        txn_aligned->ops->mmap(txn_aligned, &buffer);
        txn->ops->copyto(txn, buffer + (txn->offset - txn_aligned->offset),
                         txn->length, 0);
        actual = txn->length;
        goto done;
    } else {
        // Copy the result from the original txn into the aligned read
        void* buffer;
        txn->ops->mmap(txn, &buffer);
        txn_aligned->ops->copyto(txn_aligned, buffer, txn->length,
                                 txn->offset - txn_aligned->offset);
        txn_aligned->opcode = IOTXN_OP_WRITE;
        txn_aligned->complete_cb = aligned_write_complete;
        iotxn_queue(txn->context, txn_aligned);
        return;
    }
done:
    txn_aligned->ops->release(txn_aligned);
    txn->ops->complete(txn, status, actual);
}

static void align_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    uint64_t blksize = get_aligned_device(dev)->blksize;
    mx_device_t* parent = dev->parent;

    // In the case that the request is:
    //  1) Already aligned, or
    //  2) Not a READ / WRITE operation
    // Don't alter it.
    if ((txn->offset % blksize == 0 && txn->length % blksize == 0) ||
        (txn->opcode != IOTXN_OP_READ && txn->opcode != IOTXN_OP_WRITE)) {
        parent->ops->iotxn_queue(parent, txn);
        return;
    }

    // Rounded down to the nearest blksize
    mx_off_t offset_aligned = txn->offset - (txn->offset % blksize);
    // Increase the length to include both:
    // The portion of the first block before the offset,
    mx_off_t length_aligned = txn->length + (txn->offset % blksize);
    // and the portion of the last block after the offset
    length_aligned = (((length_aligned - 1) / blksize) + 1) * blksize;

    // Prevent overflows from mx_off_t to size_t conversions
    if ((length_aligned > SIZE_MAX) || (blksize > SIZE_MAX)) {
        txn->ops->complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    // TODO(smklein): For large iotxns, allocating an iotxn of size
    // 'length_aligned' can cause a large, unnecessary allocation. Rather than
    // re-allocating a larger iotxn, use an iotxn of length 'blksize' for the
    // misaligned / start end, and issue the original 'iotxn' for the aligned
    // middle portion of the request.

    // Allocates a larger iotxn, capable of containing the aligned length.
    iotxn_t* txn_aligned;
    mx_status_t status = iotxn_alloc(&txn_aligned, 0, length_aligned, 0);
    if (status != NO_ERROR) {
        txn->ops->complete(txn, status, 0);
        return;
    }
    txn_aligned->opcode = IOTXN_OP_READ;
    txn_aligned->offset = offset_aligned;
    txn_aligned->length = length_aligned;
    txn_aligned->complete_cb = aligned_read_complete;
    txn_aligned->cookie = txn;
    txn->context = parent;
    iotxn_queue(parent, txn_aligned);
}

static mx_off_t align_getsize(mx_device_t* dev) {
    mx_device_t* parent = dev->parent;
    return parent->ops->get_size(parent);
}

static void align_unbind(mx_device_t* dev) {
    device_remove(dev);
}

static mx_status_t align_release(mx_device_t* dev) {
    align_device_t* device = get_aligned_device(dev);
    free(device);
    return NO_ERROR;
}

static mx_protocol_device_t align_proto = {
    .ioctl = align_ioctl,
    .iotxn_queue = align_iotxn_queue,
    .get_size = align_getsize,
    .unbind = align_unbind,
    .release = align_release,
};

static mx_status_t align_bind(mx_driver_t* drv, mx_device_t* dev) {
    align_device_t* device = calloc(1, sizeof(align_device_t));
    if (!device) {
        return ERR_NO_MEMORY;
    }
    char name[MX_DEVICE_NAME_MAX + 1];
    snprintf(name, sizeof(name), "%s (aligned)", dev->name);
    device_init(&device->device, drv, name, &align_proto);
    ssize_t rc = dev->ops->ioctl(dev, IOCTL_BLOCK_GET_BLOCKSIZE, NULL, 0,
                                 &device->blksize, sizeof(&device->blksize));
    if (rc < 0) {
        free(device);
        return rc;
    }
    device->device.protocol_id = MX_PROTOCOL_BLOCK;
    mx_status_t status;
    if ((status = device_add(&device->device, dev)) != NO_ERROR) {
        free(device);
        return status;
    }
    return NO_ERROR;
}

mx_driver_t _driver_align= {
    .ops = {
        .bind = align_bind,
    },
    .flags = DRV_FLAG_NO_AUTOBIND,
};

MAGENTA_DRIVER_BEGIN(_driver_align, "align", "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK),
MAGENTA_DRIVER_END(_driver_align)
