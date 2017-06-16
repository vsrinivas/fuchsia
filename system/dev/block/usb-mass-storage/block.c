// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-mass-storage.h"

#include <stdio.h>
#include <string.h>

static void ums_block_queue(void* ctx, iotxn_t* txn) {
    ums_block_t* dev = ctx;

    if (txn->offset % dev->block_size) {
        iotxn_complete(txn, MX_ERR_INVALID_ARGS, 0);
        return;
    }
    if (txn->length % dev->block_size) {
        iotxn_complete(txn, MX_ERR_INVALID_ARGS, 0);
        return;
    }
    txn->context = dev;

    ums_t* ums = block_to_ums(dev);
    mtx_lock(&ums->iotxn_lock);
    list_add_tail(&ums->queued_iotxns, &txn->node);
    mtx_unlock(&ums->iotxn_lock);
    completion_signal(&ums->iotxn_completion);
}

static void ums_get_info(void* ctx, block_info_t* info) {
    ums_block_t* dev = ctx;
    memset(info, 0, sizeof(*info));
    info->block_size = dev->block_size;
    info->block_count = dev->total_blocks;
    info->flags = dev->flags;
}

static mx_status_t ums_block_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen,
                                   void* reply, size_t max, size_t* out_actual) {
    ums_block_t* dev = ctx;

    // TODO implement other block ioctls
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reply;
        if (max < sizeof(*info))
            return MX_ERR_BUFFER_TOO_SMALL;
        ums_get_info(dev, info);
        *out_actual = sizeof(*info);
        return MX_OK;
    }
    case IOCTL_BLOCK_RR_PART: {
        // rebind to reread the partition table
        return device_rebind(dev->mxdev);
    }
    case IOCTL_DEVICE_SYNC: {
        ums_sync_node_t node;

        ums_t* ums = block_to_ums(dev);
        mtx_lock(&ums->iotxn_lock);
        iotxn_t* txn = list_peek_tail_type(&ums->queued_iotxns, iotxn_t, node);
        if (!txn) {
            txn = ums->curr_txn;
        }
        if (!txn) {
            mtx_unlock(&ums->iotxn_lock);
            return MX_OK;
        }
        // queue a stack allocated sync node on ums_t.sync_nodes
        node.iotxn = txn;
        completion_reset(&node.completion);
        list_add_head(&ums->sync_nodes, &node.node);
        mtx_unlock(&ums->iotxn_lock);

        return completion_wait(&node.completion, MX_TIME_INFINITE);
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

static mx_off_t ums_block_get_size(void* ctx) {
    ums_block_t* dev = ctx;
    return dev->block_size * dev->total_blocks;
}

static mx_protocol_device_t ums_block_proto = {
    .version = DEVICE_OPS_VERSION,
    .iotxn_queue = ums_block_queue,
    .ioctl = ums_block_ioctl,
    .get_size = ums_block_get_size,
};

static void ums_async_set_callbacks(void* ctx, block_callbacks_t* cb) {
    ums_block_t* dev = ctx;
    dev->cb = cb;
}

static void ums_async_complete(iotxn_t* txn, void* cookie) {
    ums_block_t* dev = (ums_block_t*)txn->extra[0];
    dev->cb->complete(cookie, txn->status);
    iotxn_release(txn);
}

static void ums_async_read(void* ctx, mx_handle_t vmo, uint64_t length,
                           uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    ums_block_t* dev = ctx;

    iotxn_t* txn;
    mx_status_t status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, vmo, vmo_offset, length);
    if (status != MX_OK) {
        dev->cb->complete(cookie, status);
        return;
    }
    txn->opcode = IOTXN_OP_READ;
    txn->offset = dev_offset;
    txn->complete_cb = ums_async_complete;
    txn->cookie = cookie;
    txn->extra[0] = (uintptr_t)dev;
    iotxn_queue(dev->mxdev, txn);
}

static void ums_async_write(void* ctx, mx_handle_t vmo, uint64_t length,
                            uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    ums_block_t* dev = ctx;

    iotxn_t* txn;
    mx_status_t status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, vmo, vmo_offset, length);
    if (status != MX_OK) {
        dev->cb->complete(cookie, status);
        return;
    }
    txn->opcode = IOTXN_OP_WRITE;
    txn->offset = dev_offset;
    txn->complete_cb = ums_async_complete;
    txn->cookie = cookie;
    txn->extra[0] = (uintptr_t)dev;
    iotxn_queue(dev->mxdev, txn);
}

static block_protocol_ops_t ums_block_ops = {
    .set_callbacks = ums_async_set_callbacks,
    .get_info = ums_get_info,
    .read = ums_async_read,
    .write = ums_async_write,
};

mx_status_t ums_block_add_device(ums_t* ums, ums_block_t* dev) {
    dev->cb = NULL;

    char name[16];
    snprintf(name, sizeof(name), "lun-%03d", dev->lun);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = dev,
        .ops = &ums_block_proto,
        .proto_id = MX_PROTOCOL_BLOCK_CORE,
        .proto_ops = &ums_block_ops,
    };

    return device_add(ums->mxdev, &args, &dev->mxdev);
}
