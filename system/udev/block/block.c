// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>

#include <magenta/process.h>
#include <magenta/types.h>
#include <sys/param.h>
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <threads.h>

#include "server.h"

#define FLAG_BG_THREAD_JOINABLE       0x0001

typedef struct blkdev {
    mx_device_t device;
    block_ops_t* blockops;

    mtx_t lock;
    BlockServer* bs;
    uint32_t flags;
    thrd_t bs_thread;
} blkdev_t;

#define get_blkdev(dev) containerof(dev, blkdev_t, device)

static int blockserver_thread(void* arg) {
    blkdev_t* bdev = (blkdev_t*)arg;
    BlockServer* bs = bdev->bs;
    blockserver_serve(bs, bdev->device.parent, bdev->blockops);

    mtx_lock(&bdev->lock);
    bdev->bs = NULL;
    bdev->flags |= FLAG_BG_THREAD_JOINABLE;
    mtx_unlock(&bdev->lock);

    blockserver_free(bs);
    return 0;
}

static ssize_t blkdev_get_fifos(blkdev_t* bdev, void* out_buf, size_t out_len) {
    if (out_len < sizeof(mx_handle_t)) {
        return ERR_INVALID_ARGS;
    }
    mx_status_t status;
    mtx_lock(&bdev->lock);
    if (bdev->bs != NULL) {
        status = ERR_ALREADY_BOUND;
        goto done;
    } else if (bdev->flags & FLAG_BG_THREAD_JOINABLE) {
        // Clean up the thread that came before us
        thrd_join(bdev->bs_thread, NULL);
    }

    BlockServer* bs;
    if ((status = blockserver_create(out_buf, &bs)) != NO_ERROR) {
        goto done;
    }

    // As soon as we launch a thread, the background thread is responsible
    // for the blockserver in the bdev->bs field.
    bdev->bs = bs;
    if (thrd_create(&bdev->bs_thread, blockserver_thread, bdev) != thrd_success) {
        blockserver_free(bs);
        bdev->bs = NULL;
        status = ERR_NO_MEMORY;
        goto done;
    }

    status = sizeof(mx_handle_t);
done:
    mtx_unlock(&bdev->lock);
    return status;
}

static ssize_t blkdev_attach_vmo(blkdev_t* bdev,
                                 const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len) {
    if ((in_len < sizeof(mx_handle_t)) || (out_len < sizeof(vmoid_t))) {
        return ERR_INVALID_ARGS;
    }

    mx_status_t status;
    mtx_lock(&bdev->lock);
    if (bdev->bs == NULL) {
        status = ERR_BAD_STATE;
        goto done;
    }

    mx_handle_t h = *(mx_handle_t*)in_buf;
    if ((status = blockserver_attach_vmo(bdev->bs, h, out_buf)) != NO_ERROR) {
        goto done;
    }

    status = sizeof(vmoid_t);
done:
    mtx_unlock(&bdev->lock);
    return status;
}

static ssize_t blkdev_alloc_txn(blkdev_t* bdev,
                                const void* in_buf, size_t in_len,
                                void* out_buf, size_t out_len) {
    if ((in_len != 0) || (out_len < sizeof(txnid_t))) {
        return ERR_INVALID_ARGS;
    }

    mx_status_t status;
    mtx_lock(&bdev->lock);
    if (bdev->bs == NULL) {
        status = ERR_BAD_STATE;
        goto done;
    }

    if ((status = blockserver_allocate_txn(bdev->bs, out_buf)) != NO_ERROR) {
        goto done;
    }

    status = sizeof(txnid_t);
done:
    mtx_unlock(&bdev->lock);
    return status;
}

static ssize_t blkdev_free_txn(blkdev_t* bdev,
                               const void* in_buf, size_t in_len,
                               void* out_buf, size_t out_len) {
    if ((in_len != sizeof(txnid_t)) || (out_len != 0)) {
        return ERR_INVALID_ARGS;
    }

    mx_status_t status;
    mtx_lock(&bdev->lock);
    if (bdev->bs == NULL) {
        status = ERR_BAD_STATE;
        goto done;
    }

    txnid_t txnid = *(txnid_t*)in_buf;
    blockserver_free_txn(bdev->bs, txnid);
    status = NO_ERROR;
done:
    mtx_unlock(&bdev->lock);
    return status;
}

static ssize_t blkdev_fifo_close(blkdev_t* bdev) {
    mtx_lock(&bdev->lock);
    if (bdev->bs != NULL) {
        blockserver_shutdown(bdev->bs);
        mtx_unlock(&bdev->lock);
        thrd_join(bdev->bs_thread, NULL);
        bdev->bs = NULL;
        bdev->flags &= ~FLAG_BG_THREAD_JOINABLE;
    } else {
        // No background thread running.
        mtx_unlock(&bdev->lock);
    }

    return NO_ERROR;
}

// implement device protocol:

static ssize_t blkdev_ioctl(mx_device_t* dev, uint32_t op, const void* cmd,
                            size_t cmdlen, void* reply, size_t max) {
    blkdev_t* blkdev = get_blkdev(dev);
    switch (op) {
    case IOCTL_BLOCK_GET_FIFOS:
        return blkdev_get_fifos(blkdev, reply, max);
    case IOCTL_BLOCK_ATTACH_VMO:
        return blkdev_attach_vmo(blkdev, cmd, cmdlen, reply, max);
    case IOCTL_BLOCK_ALLOC_TXN:
        return blkdev_alloc_txn(blkdev, cmd, cmdlen, reply, max);
    case IOCTL_BLOCK_FREE_TXN:
        return blkdev_free_txn(blkdev, cmd, cmdlen, reply, max);
    case IOCTL_BLOCK_FIFO_CLOSE:
        return blkdev_fifo_close(blkdev);
    default: {
        mx_device_t* parent = dev->parent;
        return parent->ops->ioctl(parent, op, cmd, cmdlen, reply, max);
    }
    }
}

static void blkdev_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    mx_device_t* parent = dev->parent;
    parent->ops->iotxn_queue(parent, txn);
}

static mx_off_t blkdev_get_size(mx_device_t* dev) {
    mx_device_t* parent = dev->parent;
    return parent->ops->get_size(parent);
}

static void blkdev_unbind(mx_device_t* dev) {
    device_remove(dev);
}

static mx_status_t blkdev_release(mx_device_t* dev) {
    blkdev_t* blkdev = get_blkdev(dev);
    blkdev_fifo_close(blkdev);
    free(blkdev);
    return NO_ERROR;
}

static mx_protocol_device_t blkdev_ops = {
    .ioctl = blkdev_ioctl,
    .iotxn_queue = blkdev_iotxn_queue,
    .get_size = blkdev_get_size,
    .unbind = blkdev_unbind,
    .release = blkdev_release,
};

static mx_status_t block_driver_bind(mx_driver_t* drv, mx_device_t* dev, void** cookie) {
    blkdev_t* bdev;
    if ((bdev = calloc(1, sizeof(blkdev_t))) == NULL) {
        return ERR_NO_MEMORY;
    }

    mx_status_t status;
    if (device_get_protocol(dev, MX_PROTOCOL_BLOCK_CORE, (void**)&bdev->blockops)) {
        status = ERR_INTERNAL;
        goto fail;
    }

    device_init(&bdev->device, drv, "block", &blkdev_ops);
    mtx_init(&bdev->lock, mtx_plain);

    bdev->device.protocol_id = MX_PROTOCOL_BLOCK;
    if ((status = device_add(&bdev->device, dev)) != NO_ERROR) {
        goto fail;
    }

    return NO_ERROR;
fail:
    free(bdev);
    return status;
}

mx_driver_t _driver_block = {
    .ops = {
        .bind = block_driver_bind,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_block, "block", "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK_CORE),
MAGENTA_DRIVER_END(_driver_block)
