// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/metadata.h>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <lib/sync/completion.h>
#include <zircon/boot/image.h>
#include <zircon/device/block.h>
#include <zircon/process.h>
#include <zircon/types.h>

#include "server.h"

#define max(a, b) ((a) < (b) ? (b) : (a))
#define min(a, b) ((a) < (b) ? (a) : (b))

typedef struct blkdev {
    zx_device_t* zxdev;
    zx_device_t* parent;

    mtx_t lock;
    sync_completion_t lock_signal;

    uint32_t threadcount;

    block_protocol_t bp;
    block_info_t info;
    size_t block_op_size;

    BlockServer* bs;
    bool dead; // Release has been called; we should free memory and leave.

    // true if we have metadata for a ZBI partition map
    bool has_bootpart;

    mtx_t iolock;
    zx_handle_t iovmo;
    zx_status_t iostatus;
    sync_completion_t iosignal;
    block_op_t* iobop;
} blkdev_t;

static int blockserver_thread_serve(blkdev_t* bdev) {
    mtx_lock(&bdev->lock);
    // Signal when the blockserver_thread has successfully acquired the lock.
    sync_completion_signal(&bdev->lock_signal);

    BlockServer* bs = bdev->bs;
    if (!bdev->dead && (bs != NULL)) {
        mtx_unlock(&bdev->lock);
        blockserver_serve(bs);
        mtx_lock(&bdev->lock);
    }

    if (bdev->bs == bs) {
        // Only nullify 'bs' if no one has replaced it yet. This is the
        // case when the blockserver shuts itself down because the fifo
        // has closed.
        bdev->bs = NULL;
    }
    bdev->threadcount--;
    bool cleanup = bdev->dead && (bdev->threadcount == 0);
    mtx_unlock(&bdev->lock);

    if (bs != NULL) {
        blockserver_free(bs);
    }
    if (cleanup) {
        zx_handle_close(bdev->iovmo);
        free(bdev->iobop);
        free(bdev);
    }
    return 0;
}

static int blockserver_thread(void* arg) {
    return blockserver_thread_serve((blkdev_t*)arg);
}

static zx_status_t blkdev_get_fifos(blkdev_t* bdev, void* out_buf, size_t out_len,
                                    size_t* out_actual) {
    if (out_len < sizeof(zx_handle_t)) {
        return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status;
    mtx_lock(&bdev->lock);
    if (bdev->bs != NULL) {
        status = ZX_ERR_ALREADY_BOUND;
        goto unlock_exit;
    }

    BlockServer* bs;
    if ((status = blockserver_create(bdev->parent, &bdev->bp, out_buf, &bs)) != ZX_OK) {
        goto unlock_exit;
    }
    bdev->bs = bs;

    // Bump the thread count for the thread to be created
    bdev->threadcount++;
    mtx_unlock(&bdev->lock);

    // Use this completion to ensure the block server doesn't race initializing
    // with a call to teardown.
    sync_completion_reset(&bdev->lock_signal);

    thrd_t thread;
    if (thrd_create(&thread, blockserver_thread, bdev) == thrd_success) {
        thrd_detach(thread);
        sync_completion_wait(&bdev->lock_signal, ZX_TIME_INFINITE);
        *out_actual = sizeof(zx_handle_t);
        return ZX_OK;
    }

    mtx_lock(&bdev->lock);
    bdev->threadcount--;
    bdev->bs = NULL;
    mtx_unlock(&bdev->lock);

    blockserver_free(bs);
    return ZX_ERR_NO_MEMORY;

unlock_exit:
    mtx_unlock(&bdev->lock);
    return status;
}

static zx_status_t blkdev_attach_vmo(blkdev_t* bdev,
                                 const void* in_buf, size_t in_len,
                                 void* out_buf, size_t out_len, size_t* out_actual) {
    if ((in_len < sizeof(zx_handle_t)) || (out_len < sizeof(vmoid_t))) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status;
    mtx_lock(&bdev->lock);
    if (bdev->bs == NULL) {
        status = ZX_ERR_BAD_STATE;
        goto done;
    }

    zx_handle_t h = *(zx_handle_t*)in_buf;
    if ((status = blockserver_attach_vmo(bdev->bs, h, out_buf)) != ZX_OK) {
        goto done;
    }
    *out_actual = sizeof(vmoid_t);

done:
    mtx_unlock(&bdev->lock);
    return status;
}

static zx_status_t blkdev_fifo_close_locked(blkdev_t* bdev) {
    if (bdev->bs != NULL) {
        blockserver_shutdown(bdev->bs);
        // Ensure that the next thread to call "get_fifos" will
        // not see the previous block server.
        bdev->bs = NULL;
    }
    return ZX_OK;
}

static zx_status_t blkdev_rebind(blkdev_t* bdev) {
    // remove our existing children, ask to bind new children
    return device_rebind(bdev->zxdev);
}

static zx_status_t blkdev_ioctl(void* ctx, uint32_t op, const void* cmd,
                            size_t cmdlen, void* reply, size_t max, size_t* out_actual) {
    blkdev_t* blkdev = ctx;
    switch (op) {
    case IOCTL_BLOCK_GET_FIFOS:
        return blkdev_get_fifos(blkdev, reply, max, out_actual);
    case IOCTL_BLOCK_ATTACH_VMO:
        return blkdev_attach_vmo(blkdev, cmd, cmdlen, reply, max, out_actual);
    case IOCTL_BLOCK_FIFO_CLOSE: {
        mtx_lock(&blkdev->lock);
        zx_status_t status = blkdev_fifo_close_locked(blkdev);
        mtx_unlock(&blkdev->lock);
        return status;
    }
    case IOCTL_BLOCK_RR_PART:
        return blkdev_rebind(blkdev);
    case IOCTL_BLOCK_GET_INFO: {
        size_t actual;
        zx_status_t status = device_ioctl(blkdev->parent, op, cmd, cmdlen, reply, max, &actual);
        if (status == ZX_OK) {
            if (actual >= sizeof(block_info_t)) {
                block_info_t* info = (block_info_t*)reply;
                // set or clear BLOCK_FLAG_BOOTPART appropriately
                if (blkdev->has_bootpart) {
                    info->flags |= BLOCK_FLAG_BOOTPART;
                } else {
                    info->flags &= ~BLOCK_FLAG_BOOTPART;
                }
            }
            if (out_actual) {
                *out_actual = actual;
            }
        }
        return status;
    }
    default:
        // TODO: this may no longer be necessary now that we handle IOCTL_BLOCK_GET_INFO here
        return device_ioctl(blkdev->parent, op, cmd, cmdlen, reply, max, out_actual);
    }
}

static void block_completion_cb(block_op_t* bop, zx_status_t status) {
    blkdev_t* bdev = bop->cookie;
    bdev->iostatus = status;
    sync_completion_signal(&bdev->iosignal);
}

// Adapter from read/write to block_op_t
// This is technically incorrect because the read/write hooks should not block,
// but the old adapter in devhost was *also* blocking, so we're no worse off
// than before, but now localized to the block middle layer.
// TODO(swetland) plumbing in devhosts to do deferred replies

// Define the maximum I/O possible for the midlayer; this is arbitrarily
// set to the size of RIO's max payload.
//
// If a smaller value of "max_transfer_size" is defined, that will
// be used instead.
#define MAX_MIDLAYER_IO 8192

static zx_status_t blkdev_io(blkdev_t* bdev, void* buf, size_t count,
                             zx_off_t off, bool write) {
    const size_t bsz = bdev->info.block_size;
    const size_t max_xfer = min(bdev->info.max_transfer_size, MAX_MIDLAYER_IO);

    if (count == 0) {
        return ZX_OK;
    }
    if ((count % bsz) || (off % bsz)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (bdev->iovmo == ZX_HANDLE_INVALID) {
        if (zx_vmo_create(max(max_xfer, PAGE_SIZE), 0, &bdev->iovmo) != ZX_OK) {
            return ZX_ERR_INTERNAL;
        }
    }

    // TODO(smklein): These requests can be queued simultaneously without
    // blocking. However, as the comment above mentions, this code probably
    // shouldn't be blocking at all.
    uint64_t sub_txn_offset = 0;
    while (sub_txn_offset < count) {
        void* sub_buf = buf + sub_txn_offset;
        size_t sub_txn_length = min(count - sub_txn_offset, max_xfer);

        if (write) {
            if (zx_vmo_write(bdev->iovmo, sub_buf, 0, sub_txn_length) != ZX_OK) {
                return ZX_ERR_INTERNAL;
            }
        }

        block_op_t* bop = bdev->iobop;
        bop->command = write ? BLOCK_OP_WRITE : BLOCK_OP_READ;
        bop->rw.length = sub_txn_length / bsz;
        bop->rw.vmo = bdev->iovmo;
        bop->rw.offset_dev = (off + sub_txn_offset) / bsz;
        bop->rw.offset_vmo = 0;
        bop->rw.pages = NULL;
        bop->completion_cb = block_completion_cb;
        bop->cookie = bdev;

        sync_completion_reset(&bdev->iosignal);
        bdev->bp.ops->queue(bdev->bp.ctx, bop);
        sync_completion_wait(&bdev->iosignal, ZX_TIME_INFINITE);

        if (bdev->iostatus != ZX_OK) {
            return bdev->iostatus;
        }

        if (!write) {
            if (zx_vmo_read(bdev->iovmo, buf + sub_txn_offset, 0, sub_txn_length) != ZX_OK) {
                return ZX_ERR_INTERNAL;
            }
        }
        sub_txn_offset += sub_txn_length;
    }

    return bdev->iostatus;
}

static zx_status_t blkdev_read(void* ctx, void* buf, size_t count,
                               zx_off_t off, size_t* actual) {
    blkdev_t* bdev = ctx;
    mtx_lock(&bdev->iolock);
    zx_status_t status = blkdev_io(bdev, buf, count, off, false);
    mtx_unlock(&bdev->iolock);
    *actual = (status == ZX_OK) ? count : 0;
    return status;
}

static zx_status_t blkdev_write(void* ctx, const void* buf, size_t count,
                                zx_off_t off, size_t* actual) {
    blkdev_t* bdev = ctx;
    mtx_lock(&bdev->iolock);
    zx_status_t status = blkdev_io(bdev, (void*) buf, count, off, true);
    mtx_unlock(&bdev->iolock);
    *actual = (status == ZX_OK) ? count : 0;
    return status;
}

static zx_off_t blkdev_get_size(void* ctx) {
    blkdev_t* bdev = ctx;
    //TODO: use query() results, *but* fvm returns different query and getsize
    // results, and the latter are dynamic...
    return device_get_size(bdev->parent);
    //return bdev->info.block_count * bdev->info.block_size;
}

static void blkdev_unbind(void* ctx) {
    blkdev_t* blkdev = ctx;
    device_remove(blkdev->zxdev);
}

static void blkdev_release(void* ctx) {
    blkdev_t* blkdev = ctx;
    mtx_lock(&blkdev->lock);
    bool bg_thread_running = (blkdev->threadcount != 0);
    blkdev_fifo_close_locked(blkdev);
    blkdev->dead = true;
    mtx_unlock(&blkdev->lock);

    if (!bg_thread_running) {
        // If it isn't running, we need to clean up.
        // Otherwise, it'll free blkdev's memory when it's done,
        // since (1) no one else can call get_fifos anymore, and
        // (2) it'll clean up when it sees that blkdev is dead.
        zx_handle_close(blkdev->iovmo);
        free(blkdev->iobop);
        free(blkdev);
    }
}

static void block_query(void* ctx, block_info_t* bi, size_t* bopsz) {
    blkdev_t* bdev = ctx;
    memcpy(bi, &bdev->info, sizeof(block_info_t));
    *bopsz = bdev->block_op_size;
}

static void block_queue(void* ctx, block_op_t* bop) {
    blkdev_t* bdev = ctx;
    bdev->bp.ops->queue(bdev->bp.ctx, bop);
}

static block_protocol_ops_t block_ops = {
    .query = block_query,
    .queue = block_queue,
};

static zx_protocol_device_t blkdev_ops = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = blkdev_ioctl,
    .read = blkdev_read,
    .write = blkdev_write,
    .get_size = blkdev_get_size,
    .unbind = blkdev_unbind,
    .release = blkdev_release,
};

static zx_status_t block_driver_bind(void* ctx, zx_device_t* dev) {
    blkdev_t* bdev;
    if ((bdev = calloc(1, sizeof(blkdev_t))) == NULL) {
        return ZX_ERR_NO_MEMORY;
    }
    mtx_init(&bdev->lock, mtx_plain);
    bdev->parent = dev;

    if (device_get_protocol(dev, ZX_PROTOCOL_BLOCK_IMPL, &bdev->bp) != ZX_OK) {
        printf("ERROR: block device '%s': does not support block protocol\n",
               device_get_name(dev));
        free(bdev);
        return ZX_ERR_NOT_SUPPORTED;
    }

    bdev->bp.ops->query(bdev->bp.ctx, &bdev->info, &bdev->block_op_size);

    if (bdev->info.max_transfer_size < bdev->info.block_size) {
        printf("ERROR: block device '%s': has smaller max xfer (0x%x) than block size (0x%x)\n",
               device_get_name(dev), bdev->info.max_transfer_size, bdev->info.block_size);
        free(bdev);
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status;
    if ((bdev->iobop = malloc(bdev->block_op_size)) == NULL) {
        status = ZX_ERR_NO_MEMORY;
        goto fail;
    }

    size_t bsz = bdev->info.block_size;
    if ((bsz < 512) || (bsz & (bsz - 1))){
        printf("block: device '%s': invalid block size: %zu\n",
               device_get_name(dev), bsz);
        status = ZX_ERR_NOT_SUPPORTED;
        goto fail;
    }

    // check to see if we have a ZBI partition map
    // and set BLOCK_FLAG_BOOTPART accordingly
    uint8_t buffer[METADATA_PARTITION_MAP_MAX];
    size_t actual;
    status = device_get_metadata(dev, DEVICE_METADATA_PARTITION_MAP, buffer, sizeof(buffer),
                                 &actual);
    if (status == ZX_OK && actual >= sizeof(zbi_partition_map_t)) {
        bdev->has_bootpart = true;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "block",
        .ctx = bdev,
        .ops = &blkdev_ops,
        .proto_id = ZX_PROTOCOL_BLOCK,
        .proto_ops = &block_ops,
    };

    status = device_add(dev, &args, &bdev->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    return ZX_OK;

fail:
    free(bdev->iobop);
    free(bdev);
    return status;
}

static zx_driver_ops_t block_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = block_driver_bind,
};

ZIRCON_DRIVER_BEGIN(block, block_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK_IMPL),
ZIRCON_DRIVER_END(block)
