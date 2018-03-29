// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>

#include <zircon/device/ramdisk.h>
#include <sync/completion.h>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <zircon/device/block.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#define MAX_TRANSFER_SIZE (1 << 19)

typedef struct {
    zx_device_t* zxdev;
} ramctl_device_t;

typedef struct ramdisk_device {
    zx_device_t* zxdev;
    uintptr_t mapped_addr;
    uint64_t blk_size;
    uint64_t blk_count;

    mtx_t lock;
    completion_t signal;
    list_node_t txn_list;
    bool dead;

    uint32_t flags;
    zx_handle_t vmo;

    bool asleep; // true if the ramdisk is "sleeping"
    uint64_t sa_txn_count; // number of transactions to sleep after
    ramdisk_txn_counts_t txn_counts; // current transaction counts

    thrd_t worker;
    char name[NAME_MAX];
} ramdisk_device_t;

typedef struct {
    block_op_t op;
    list_node_t node;
} ramdisk_txn_t;

// The worker thread processes messages from iotxns in the background
static int worker_thread(void* arg) {
    zx_status_t status = ZX_OK;
    ramdisk_device_t* dev = (ramdisk_device_t*)arg;
    ramdisk_txn_t* txn = NULL;
    bool dead, asleep;

    for (;;) {
        for (;;) {
            mtx_lock(&dev->lock);
            dead = dev->dead;
            // Increment the count if the previous transaction completed.
            if (txn != NULL) {
                if (status == ZX_OK) {
                    dev->txn_counts.successful++;
                } else {
                    dev->txn_counts.failed++;
                }
                // Put the ramdisk to sleep if we have reached the required # of transactions
                if (dev->sa_txn_count != 0) {
                    --dev->sa_txn_count;
                    dev->asleep = (dev->sa_txn_count == 0);
                }
            }
            // Grab the next transaction unless the device is saving them until it wakes
            asleep = dev->asleep;
            if (!dead && asleep && (dev->flags & RAMDISK_FLAG_RESUME_ON_WAKE) != 0) {
                txn = NULL;
            } else {
                txn = list_remove_head_type(&dev->txn_list, ramdisk_txn_t, node);
            }
            mtx_unlock(&dev->lock);
            if (dead) {
                goto goodbye;
            }
            if (txn == NULL) {
                completion_wait(&dev->signal, ZX_TIME_INFINITE);
            } else {
                completion_reset(&dev->signal);
                break;
            }
        }

        void* addr = (void*) dev->mapped_addr + txn->op.rw.offset_dev;
        size_t len = txn->op.rw.length * dev->blk_size;

        if (len > MAX_TRANSFER_SIZE) {
            txn->op.completion_cb(&txn->op, ZX_ERR_OUT_OF_RANGE);
            continue;
        }

        if (asleep) {
            status = ZX_ERR_UNAVAILABLE;
        } else if (txn->op.command == BLOCK_OP_READ) {
            status = zx_vmo_write(txn->op.rw.vmo, addr, txn->op.rw.offset_vmo, len);
        } else { // BLOCK_OP_WRITE
            status = zx_vmo_read(txn->op.rw.vmo, addr, txn->op.rw.offset_vmo, len);
        }
        txn->op.completion_cb(&txn->op, status);
    }

goodbye:
    while (txn != NULL) {
        txn->op.completion_cb(&txn->op, ZX_ERR_BAD_STATE);
        mtx_lock(&dev->lock);
        txn = list_remove_head_type(&dev->txn_list, ramdisk_txn_t, node);
        mtx_unlock(&dev->lock);
    }
    return 0;
}

static uint64_t sizebytes(ramdisk_device_t* rdev) {
    return rdev->blk_size * rdev->blk_count;
}

static void ramdisk_get_info(void* ctx, block_info_t* info) {
    ramdisk_device_t* ramdev = ctx;
    memset(info, 0, sizeof(*info));
    info->block_size = ramdev->blk_size;
    info->block_count = ramdev->blk_count;
    // Arbitrarily set, but matches the SATA driver for testing
    info->max_transfer_size = MAX_TRANSFER_SIZE;
    info->flags = ramdev->flags;
}

// implement device protocol:

static void ramdisk_unbind(void* ctx) {
    ramdisk_device_t* ramdev = ctx;
    mtx_lock(&ramdev->lock);
    ramdev->dead = true;
    mtx_unlock(&ramdev->lock);
    completion_signal(&ramdev->signal);
    device_remove(ramdev->zxdev);
}

static zx_status_t ramdisk_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmd_len,
                                 void* reply, size_t max, size_t* out_actual) {
    ramdisk_device_t* ramdev = ctx;
    if (ramdev->dead) {
        return ZX_ERR_BAD_STATE;
    }

    switch (op) {
    case IOCTL_RAMDISK_UNLINK: {
        ramdisk_unbind(ramdev);
        return ZX_OK;
    }
    case IOCTL_RAMDISK_SET_FLAGS: {
        if (cmd_len < sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        uint32_t* flags = (uint32_t*)cmd;
        ramdev->flags = *flags;
        return ZX_OK;
    }
    case IOCTL_RAMDISK_WAKE_UP: {
        // Reset state and transaction counts
        mtx_lock(&ramdev->lock);
        ramdev->asleep = false;
        memset(&ramdev->txn_counts, 0, sizeof(ramdev->txn_counts));
        ramdev->sa_txn_count = 0;
        mtx_unlock(&ramdev->lock);
        completion_signal(&ramdev->signal);
        return ZX_OK;
    }
    case IOCTL_RAMDISK_SLEEP_AFTER: {
        if (cmd_len < sizeof(uint64_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        uint64_t* txn_count = (uint64_t*)cmd;
        mtx_lock(&ramdev->lock);
        ramdev->asleep = false;
        memset(&ramdev->txn_counts, 0, sizeof(ramdev->txn_counts));
        ramdev->sa_txn_count = *txn_count;
        if (*txn_count == 0) {
            ramdev->asleep = true;
        }
        mtx_unlock(&ramdev->lock);
        return ZX_OK;
    }
    case IOCTL_RAMDISK_GET_TXN_COUNTS: {
        if (max < sizeof(ramdisk_txn_counts_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        mtx_lock(&ramdev->lock);
        memcpy(reply, &ramdev->txn_counts, sizeof(ramdisk_txn_counts_t));
        mtx_unlock(&ramdev->lock);
        *out_actual = sizeof(ramdisk_txn_counts_t);
        return ZX_OK;
    }
    // Block Protocol
    case IOCTL_BLOCK_GET_NAME: {
        char* name = reply;
        memset(name, 0, max);
        strncpy(name, ramdev->name, max);
        *out_actual = strnlen(name, max);
        return ZX_OK;
    }
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reply;
        if (max < sizeof(*info))
            return ZX_ERR_BUFFER_TOO_SMALL;
        ramdisk_get_info(ramdev, info);
        *out_actual = sizeof(*info);
        return ZX_OK;
    }
    case IOCTL_DEVICE_SYNC: {
        // Wow, we sync so quickly!
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void ramdisk_queue(void* ctx, block_op_t* bop) {
    ramdisk_device_t* ramdev = ctx;
    ramdisk_txn_t* txn = containerof(bop, ramdisk_txn_t, op);
    bool dead;

    switch ((txn->op.command &= BLOCK_OP_MASK)) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
        if ((txn->op.rw.offset_dev >= ramdev->blk_count) ||
            ((ramdev->blk_count - txn->op.rw.offset_dev) < txn->op.rw.length)) {
            bop->completion_cb(bop, ZX_ERR_OUT_OF_RANGE);
            return;
        }
        txn->op.rw.offset_dev *= ramdev->blk_size;
        txn->op.rw.offset_vmo *= ramdev->blk_size;

        mtx_lock(&ramdev->lock);
        if (!(dead = ramdev->dead)) {
            ramdev->txn_counts.received++;
            list_add_tail(&ramdev->txn_list, &txn->node);
        }
        mtx_unlock(&ramdev->lock);
        if (dead) {
            bop->completion_cb(bop, ZX_ERR_BAD_STATE);
        } else {
            completion_signal(&ramdev->signal);
        }
        break;
    case BLOCK_OP_FLUSH:
        bop->completion_cb(bop, ZX_OK);
        break;
    default:
        bop->completion_cb(bop, ZX_ERR_NOT_SUPPORTED);
        break;
    }
}

static void ramdisk_query(void* ctx, block_info_t* bi, size_t* bopsz) {
    ramdisk_get_info(ctx, bi);
    *bopsz = sizeof(ramdisk_txn_t);
}

static zx_off_t ramdisk_getsize(void* ctx) {
    return sizebytes(ctx);
}

static void ramdisk_release(void* ctx) {
    ramdisk_device_t* ramdev = ctx;

    // Wake up the worker thread, in case it is sleeping
    mtx_lock(&ramdev->lock);
    ramdev->dead = true;
    mtx_unlock(&ramdev->lock);
    completion_signal(&ramdev->signal);

    int r;
    thrd_join(ramdev->worker, &r);
    if (ramdev->vmo != ZX_HANDLE_INVALID) {
        zx_vmar_unmap(zx_vmar_root_self(), ramdev->mapped_addr, sizebytes(ramdev));
        zx_handle_close(ramdev->vmo);
    }
    free(ramdev);
}

static block_protocol_ops_t block_ops = {
    .query = ramdisk_query,
    .queue = ramdisk_queue,
};

static zx_protocol_device_t ramdisk_instance_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = ramdisk_ioctl,
    .get_size = ramdisk_getsize,
    .unbind = ramdisk_unbind,
    .release = ramdisk_release,
};

// implement device protocol:

static uint64_t ramdisk_count = 0;

// This always consumes the VMO handle.
static zx_status_t ramctl_config(ramctl_device_t* ramctl, zx_handle_t vmo,
                                 uint64_t blk_size, uint64_t blk_count,
                                 void* reply, size_t max, size_t* out_actual) {
    zx_status_t status = ZX_ERR_INVALID_ARGS;
    if (max < 32) {
        goto fail;
    }

    ramdisk_device_t* ramdev = calloc(1, sizeof(ramdisk_device_t));
    if (!ramdev) {
        status = ZX_ERR_NO_MEMORY;
        goto fail;
    }
    if (mtx_init(&ramdev->lock, mtx_plain) != thrd_success) {
        goto fail_free;
    }
    ramdev->vmo = vmo;
    ramdev->blk_size = blk_size;
    ramdev->blk_count = blk_count;
    snprintf(ramdev->name, sizeof(ramdev->name),
             "ramdisk-%" PRIu64, ramdisk_count++);

    status = zx_vmar_map(zx_vmar_root_self(), 0, ramdev->vmo, 0, sizebytes(ramdev),
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                         &ramdev->mapped_addr);
    if (status != ZX_OK) {
        goto fail_mtx;
    }
    list_initialize(&ramdev->txn_list);
    if (thrd_create(&ramdev->worker, worker_thread, ramdev) != thrd_success) {
        goto fail_unmap;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = ramdev->name,
        .ctx = ramdev,
        .ops = &ramdisk_instance_proto,
        .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
        .proto_ops = &block_ops,
    };

    if ((status = device_add(ramctl->zxdev, &args, &ramdev->zxdev)) != ZX_OK) {
        ramdisk_release(ramdev);
        return status;
    }
    strcpy(reply, ramdev->name);
    *out_actual = strlen(reply);
    return ZX_OK;

fail_unmap:
    zx_vmar_unmap(zx_vmar_root_self(), ramdev->mapped_addr, sizebytes(ramdev));
fail_mtx:
    mtx_destroy(&ramdev->lock);
fail_free:
    free(ramdev);
fail:
    zx_handle_close(vmo);
    return status;

}

static zx_status_t ramctl_ioctl(void* ctx, uint32_t op, const void* cmd,
                                size_t cmdlen, void* reply, size_t max, size_t* out_actual) {
    ramctl_device_t* ramctl = ctx;

    switch (op) {
    case IOCTL_RAMDISK_CONFIG: {
        if (cmdlen != sizeof(ramdisk_ioctl_config_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        ramdisk_ioctl_config_t* config = (ramdisk_ioctl_config_t*)cmd;
        zx_handle_t vmo;
        zx_status_t status = zx_vmo_create(
            config->blk_size * config->blk_count, 0, &vmo);
        if (status == ZX_OK) {
            status = ramctl_config(ramctl, vmo,
                                   config->blk_size, config->blk_count,
                                   reply, max, out_actual);
        }
        return status;
    }
    case IOCTL_RAMDISK_CONFIG_VMO: {
        if (cmdlen != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx_handle_t vmo = *(zx_handle_t*)cmd;

        // Ensure this is the last handle to this VMO; otherwise, the size
        // may change from underneath us.
        zx_info_handle_count_t info;
        zx_status_t status = zx_object_get_info(vmo, ZX_INFO_HANDLE_COUNT,
                                                &info, sizeof(info),
                                                NULL, NULL);
        if (status != ZX_OK || info.handle_count != 1) {
            zx_handle_close(vmo);
            return ZX_ERR_INVALID_ARGS;
        }

        uint64_t vmo_size;
        status = zx_vmo_get_size(vmo, &vmo_size);
        if (status != ZX_OK) {
            zx_handle_close(vmo);
            return status;
        }

        return ramctl_config(ramctl, vmo,
                             PAGE_SIZE, (vmo_size + PAGE_SIZE - 1) / PAGE_SIZE,
                             reply, max, out_actual);
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_protocol_device_t ramdisk_ctl_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = ramctl_ioctl,
};

static zx_status_t ramdisk_driver_bind(void* ctx, zx_device_t* parent) {
    ramctl_device_t* ramctl = calloc(1, sizeof(ramctl_device_t));
    if (ramctl == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "ramctl",
        .ops = &ramdisk_ctl_proto,
        .ctx = ramctl,
    };

    return device_add(parent, &args, &ramctl->zxdev);
}

static zx_driver_ops_t ramdisk_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ramdisk_driver_bind,
};

ZIRCON_DRIVER_BEGIN(ramdisk, ramdisk_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(ramdisk)
