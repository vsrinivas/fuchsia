// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-mass-storage.h"

#include <ddk/debug.h>

#include <stdio.h>
#include <string.h>

static void ums_block_queue(void* ctx, block_op_t* op, block_impl_queue_callback completion_cb,
                            void* cookie) {
    ums_block_t* dev = static_cast<ums_block_t*>(ctx);
    ums_txn_t* txn = block_op_to_txn(op);
    txn->completion_cb = completion_cb;
    txn->cookie = cookie;

    switch (op->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
        zxlogf(TRACE, "UMS QUEUE %s %u @%zu (%p)\n",
               (op->command & BLOCK_OP_MASK) == BLOCK_OP_READ ? "RD" : "WR",
               op->rw.length, op->rw.offset_dev, op);
        break;
    case BLOCK_OP_FLUSH:
        zxlogf(TRACE, "UMS QUEUE FLUSH (%p)\n", op);
        break;
    default:
        zxlogf(ERROR, "ums_block_queue: unsupported command %u\n", op->command);
        completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, &txn->op);
        return;
    }

    ums_t* ums = block_to_ums(dev);
    txn->dev = dev;

    mtx_lock(&ums->txn_lock);
    list_add_tail(&ums->queued_txns, &txn->node);
    mtx_unlock(&ums->txn_lock);
    sync_completion_signal(&ums->txn_completion);
}

static void ums_get_info(void* ctx, block_info_t* info) {
    ums_block_t* dev = static_cast<ums_block_t*>(ctx);
    ums_t* ums = block_to_ums(dev);
    memset(info, 0, sizeof(*info));
    info->block_size = dev->block_size;
    info->block_count = dev->total_blocks;
    info->max_transfer_size = static_cast<uint32_t>(ums->max_transfer);
    info->flags = dev->flags;
}

static void ums_block_query(void* ctx, block_info_t* info_out, size_t* block_op_size_out) {
    ums_get_info(ctx, info_out);
    *block_op_size_out = sizeof(ums_txn_t);
}

static block_impl_protocol_ops_t ums_block_ops = []() {
    block_impl_protocol_ops_t ops = {};
    ops.query = ums_block_query;
    ops.queue = ums_block_queue;
    return ops;
}();

static zx_status_t ums_block_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen,
                                   void* reply, size_t max, size_t* out_actual) {
    ums_block_t* dev = static_cast<ums_block_t*>(ctx);

    // TODO implement other block ioctls
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = static_cast<block_info_t*>(reply);
        if (max < sizeof(*info))
            return ZX_ERR_BUFFER_TOO_SMALL;
        ums_get_info(dev, info);
        *out_actual = sizeof(*info);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_off_t ums_block_get_size(void* ctx) {
    ums_block_t* dev = static_cast<ums_block_t*>(ctx);
    ;
    return dev->block_size * dev->total_blocks;
}

static zx_protocol_device_t ums_block_proto = []() {
    zx_protocol_device_t ops = {};
    ops.version = DEVICE_OPS_VERSION;
    ops.ioctl = ums_block_ioctl;
    ops.get_size = ums_block_get_size;
    return ops;
}();

extern "C" {
zx_status_t ums_block_add_device(ums_t* ums, ums_block_t* dev) {
    char name[16];
    snprintf(name, sizeof(name), "lun-%03d", dev->lun);
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = name;
    args.ctx = dev;
    args.ops = const_cast<zx_protocol_device_t*>(&ums_block_proto);
    args.proto_id = ZX_PROTOCOL_BLOCK_IMPL;
    args.proto_ops = const_cast<block_impl_protocol_ops_t*>(&ums_block_ops);
    return device_add(ums->zxdev, &args, &dev->zxdev);
}
}
