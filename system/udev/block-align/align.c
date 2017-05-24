// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include <sync/completion.h>
#include <limits.h>
#include <threads.h>

// This block device aligns all incoming requests to the size of the underlying
// block device, giving the underlying block device the appearance of a regular
// file.

typedef struct align_device {
    mx_device_t* mxdev;
    mx_device_t* parent;
    uint64_t blksize;
} align_device_t;

// implement device protocol:

static mx_status_t align_ioctl(void* ctx, uint32_t op, const void* cmd,
                               size_t cmdlen, void* reply, size_t max, size_t* out_actual) {
    align_device_t* device = ctx;
    return device_op_ioctl(device->parent, op, cmd, cmdlen, reply, max, out_actual);
}

static void aligned_write_complete(iotxn_t* txn_aligned, void* cookie) {
    iotxn_t* txn = cookie;
    mx_status_t status = txn_aligned->status;
    iotxn_release(txn_aligned);
    iotxn_complete(txn, status, txn->length);
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
        iotxn_mmap(txn_aligned, &buffer);
        iotxn_copyto(txn, buffer + (txn->offset - txn_aligned->offset),
                         txn->length, 0);
        actual = txn->length;
        goto done;
    } else {
        // Copy the result from the original txn into the aligned read
        void* buffer;
        iotxn_mmap(txn, &buffer);
        iotxn_copyto(txn_aligned, buffer, txn->length,
                                 txn->offset - txn_aligned->offset);
        txn_aligned->opcode = IOTXN_OP_WRITE;
        txn_aligned->complete_cb = aligned_write_complete;
        iotxn_queue(txn->context, txn_aligned);
        return;
    }
done:
    iotxn_release(txn_aligned);
    iotxn_complete(txn, status, actual);
}

static void align_iotxn_queue(void* ctx, iotxn_t* txn) {
    align_device_t* device = ctx;
    uint64_t blksize = device->blksize;

    // In the case that the request is:
    //  1) Already aligned, or
    //  2) Not a READ / WRITE operation
    // Don't alter it.
    if ((txn->offset % blksize == 0 && txn->length % blksize == 0) ||
        (txn->opcode != IOTXN_OP_READ && txn->opcode != IOTXN_OP_WRITE)) {
        iotxn_queue(device->parent, txn);
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
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    // TODO(smklein): For large iotxns, allocating an iotxn of size
    // 'length_aligned' can cause a large, unnecessary allocation. Rather than
    // re-allocating a larger iotxn, use an iotxn of length 'blksize' for the
    // misaligned / start end, and issue the original 'iotxn' for the aligned
    // middle portion of the request.

    // Allocates a larger iotxn, capable of containing the aligned length.
    iotxn_t* txn_aligned;
    mx_status_t status = iotxn_alloc(&txn_aligned, IOTXN_ALLOC_CONTIGUOUS | IOTXN_ALLOC_POOL, length_aligned);
    if (status != NO_ERROR) {
        iotxn_complete(txn, status, 0);
        return;
    }
    txn_aligned->opcode = IOTXN_OP_READ;
    txn_aligned->offset = offset_aligned;
    txn_aligned->length = length_aligned;
    txn_aligned->complete_cb = aligned_read_complete;
    txn_aligned->cookie = txn;
    txn->context = device->parent;
    iotxn_queue(device->parent, txn_aligned);
}

static mx_off_t align_getsize(void* ctx) {
    align_device_t* device = ctx;
    return device_op_get_size(device->parent);
}

static void align_unbind(void* ctx) {
    align_device_t* device = ctx;
    device_remove(device->mxdev);
}

static void align_release(void* ctx) {
    align_device_t* device = ctx;
    free(device);
}

static mx_protocol_device_t align_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = align_ioctl,
    .iotxn_queue = align_iotxn_queue,
    .get_size = align_getsize,
    .unbind = align_unbind,
    .release = align_release,
};

static mx_status_t align_bind(void* ctx, mx_device_t* dev, void** cookie) {
    align_device_t* device = calloc(1, sizeof(align_device_t));
    if (!device) {
        return ERR_NO_MEMORY;
    }
    device->parent = dev;

    block_info_t info;
    size_t actual = 0;
    mx_status_t rc = device_op_ioctl(dev, IOCTL_BLOCK_GET_INFO, NULL, 0, &info, sizeof(info),
                                     &actual);
    if (rc < 0) {
        free(device);
        return rc;
    } else if (actual != sizeof(info)) {
        return ERR_INTERNAL;
    }
    device->blksize = info.block_size;

    char name[MX_DEVICE_NAME_MAX + 1];
    snprintf(name, sizeof(name), "%s (aligned)", device_get_name(dev));

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = device,
        .ops = &align_proto,
        .proto_id = MX_PROTOCOL_BLOCK,
    };

    mx_status_t status;
    if ((status = device_add(dev, &args, &device->mxdev)) != NO_ERROR) {
        free(device);
        return status;
    }
    return NO_ERROR;
}

static mx_driver_ops_t align_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = align_bind,
};

MAGENTA_DRIVER_BEGIN(align, align_driver_ops, "magenta", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK),
MAGENTA_DRIVER_END(align)
