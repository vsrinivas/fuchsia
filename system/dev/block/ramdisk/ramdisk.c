// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>
#include <zircon/device/ramdisk.h>
#include <sync/completion.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <zircon/listnode.h>
#include <sys/param.h>
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <threads.h>

typedef struct {
    zx_device_t* zxdev;
} ramctl_device_t;

typedef struct ramdisk_device {
    zx_device_t* zxdev;
    uint64_t blk_size;
    uint64_t blk_count;
    uint32_t flags;
    zx_handle_t vmo;
    uintptr_t mapped_addr;
    block_callbacks_t* cb;
    char name[NAME_MAX];

    mtx_t lock;
    bool dead;
    thrd_t worker;
    cnd_t work_cvar;
    list_node_t txn_list;
} ramdisk_device_t;

// The worker thread processes messages from iotxns in the background
static int worker_thread(void* arg) {
    ramdisk_device_t* dev = (ramdisk_device_t*)arg;
    iotxn_t* txn;

    mtx_lock(&dev->lock);
    while (true) {
        while ((txn = list_remove_head_type(&dev->txn_list, iotxn_t, node)) == NULL) {
            if (dev->dead) {
                goto done;
            }
            cnd_wait(&dev->work_cvar, &dev->lock);
        }

        mtx_unlock(&dev->lock);
        switch (txn->opcode) {
            case IOTXN_OP_READ: {
                iotxn_copyto(txn, (void*) dev->mapped_addr + txn->offset, txn->length, 0);
                iotxn_complete(txn, ZX_OK, txn->length);
                break;
            }
            case IOTXN_OP_WRITE: {
                iotxn_copyfrom(txn, (void*) dev->mapped_addr + txn->offset, txn->length, 0);
                iotxn_complete(txn, ZX_OK, txn->length);
                break;
            }
            default: {
                iotxn_complete(txn, ZX_ERR_INVALID_ARGS, 0);
            }
        }
        mtx_lock(&dev->lock);
    }
done:
    mtx_unlock(&dev->lock);
    return 0;
}

static uint64_t sizebytes(ramdisk_device_t* rdev) {
    return rdev->blk_size * rdev->blk_count;
}

static zx_status_t constrain_args(ramdisk_device_t* ramdev,
                                  zx_off_t* offset, zx_off_t* length) {
    // Offset must be aligned
    if (*offset % ramdev->blk_size != 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Constrain to device capacity
    *length = MIN(*length, sizebytes(ramdev) - *offset);

    // Length must be aligned
    if (*length % ramdev->blk_size != 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    return ZX_OK;
}

static void ramdisk_get_info(void* ctx, block_info_t* info) {
    ramdisk_device_t* ramdev = ctx;
    memset(info, 0, sizeof(*info));
    info->block_size = ramdev->blk_size;
    info->block_count = sizebytes(ramdev) / ramdev->blk_size;
    // Arbitrarily set, but matches the SATA driver for testing
    info->max_transfer_size = (1 << 25);
    info->flags = ramdev->flags;
}

static void ramdisk_fifo_set_callbacks(void* ctx, block_callbacks_t* cb) {
    ramdisk_device_t* rdev = ctx;
    rdev->cb = cb;
}

static void ramdisk_fifo_read(void* ctx, zx_handle_t vmo, uint64_t length,
                              uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    ramdisk_device_t* rdev = ctx;
    zx_off_t len = length;
    zx_status_t status = constrain_args(rdev, &dev_offset, &len);
    if (status != ZX_OK) {
        rdev->cb->complete(cookie, status);
        return;
    }

    mtx_lock(&rdev->lock);
    if (rdev->dead) {
        status = ZX_ERR_BAD_STATE;
    } else {
        size_t actual;
        // Reading from disk --> Write to file VMO
        status = zx_vmo_write(vmo, (void*)rdev->mapped_addr + dev_offset,
                              vmo_offset, len, &actual);
    }
    mtx_unlock(&rdev->lock);
    rdev->cb->complete(cookie, status);
}

static void ramdisk_fifo_write(void* ctx, zx_handle_t vmo, uint64_t length,
                               uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    ramdisk_device_t* rdev = ctx;
    zx_off_t len = length;
    zx_status_t status = constrain_args(rdev, &dev_offset, &len);
    if (status != ZX_OK) {
        rdev->cb->complete(cookie, status);
        return;
    }

    mtx_lock(&rdev->lock);
    if (rdev->dead) {
        status = ZX_ERR_BAD_STATE;
    } else {
        size_t actual = 0;
        // Writing to disk --> Read from file VMO
        status = zx_vmo_read(vmo, (void*)rdev->mapped_addr + dev_offset,
                             vmo_offset, len, &actual);
    }
    mtx_unlock(&rdev->lock);
    rdev->cb->complete(cookie, status);
}

static block_protocol_ops_t ramdisk_block_ops = {
    .set_callbacks = ramdisk_fifo_set_callbacks,
    .get_info = ramdisk_get_info,
    .read = ramdisk_fifo_read,
    .write = ramdisk_fifo_write,
};

// implement device protocol:

static void ramdisk_unbind(void* ctx) {
    ramdisk_device_t* ramdev = ctx;
    mtx_lock(&ramdev->lock);
    ramdev->dead = true;
    mtx_unlock(&ramdev->lock);
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
    case IOCTL_BLOCK_RR_PART: {
        return device_rebind(ramdev->zxdev);
    }
    case IOCTL_DEVICE_SYNC: {
        // Wow, we sync so quickly!
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void ramdisk_iotxn_queue(void* ctx, iotxn_t* txn) {
    ramdisk_device_t* ramdev = ctx;
    if (ramdev->dead) {
        iotxn_complete(txn, ZX_ERR_BAD_STATE, 0);
        return;
    }
    zx_status_t status = constrain_args(ramdev, &txn->offset, &txn->length);
    if (status != ZX_OK) {
        iotxn_complete(txn, status, 0);
        return;
    }

    mtx_lock(&ramdev->lock);
    list_add_tail(&ramdev->txn_list, &txn->node);
    cnd_signal(&ramdev->work_cvar);
    mtx_unlock(&ramdev->lock);
}

static zx_off_t ramdisk_getsize(void* ctx) {
    return sizebytes(ctx);
}

static void ramdisk_release(void* ctx) {
    ramdisk_device_t* ramdev = ctx;

    // Wake up the worker thread, in case it is sleeping
    mtx_lock(&ramdev->lock);
    ramdev->dead = true;
    cnd_signal(&ramdev->work_cvar);
    mtx_unlock(&ramdev->lock);

    int r;
    thrd_join(ramdev->worker, &r);
    if (ramdev->vmo != ZX_HANDLE_INVALID) {
        zx_vmar_unmap(zx_vmar_root_self(), ramdev->mapped_addr, sizebytes(ramdev));
        zx_handle_close(ramdev->vmo);
    }
    free(ramdev);
}

static zx_protocol_device_t ramdisk_instance_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = ramdisk_ioctl,
    .iotxn_queue = ramdisk_iotxn_queue,
    .get_size = ramdisk_getsize,
    .unbind = ramdisk_unbind,
    .release = ramdisk_release,
};

// implement device protocol:

static uint64_t ramdisk_count = 0;

static zx_status_t ramctl_ioctl(void* ctx, uint32_t op, const void* cmd,
                                size_t cmdlen, void* reply, size_t max, size_t* out_actual) {
    ramctl_device_t* ramctl = ctx;

    switch (op) {
    case IOCTL_RAMDISK_CONFIG: {
        if (cmdlen != sizeof(ramdisk_ioctl_config_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        if (max < 32) {
            return ZX_ERR_INVALID_ARGS;
        }
        ramdisk_ioctl_config_t* config = (ramdisk_ioctl_config_t*)cmd;

        ramdisk_device_t* ramdev = calloc(1, sizeof(ramdisk_device_t));
        if (!ramdev) {
            return ZX_ERR_NO_MEMORY;
        }
        ramdev->blk_size = config->blk_size;
        ramdev->blk_count = config->blk_count;
        mtx_init(&ramdev->lock, mtx_plain);
        sprintf(ramdev->name, "ramdisk-%lu", ramdisk_count++);
        zx_status_t status;
        if ((status = zx_vmo_create(sizebytes(ramdev), 0, &ramdev->vmo)) != ZX_OK) {
            goto fail;
        }
        if ((status = zx_vmar_map(zx_vmar_root_self(), 0, ramdev->vmo, 0, sizebytes(ramdev),
                                  ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                  &ramdev->mapped_addr)) != ZX_OK) {
            goto fail_close_vmo;
        }
        if (cnd_init(&ramdev->work_cvar) != thrd_success) {
            goto fail_unmap;
        }
        list_initialize(&ramdev->txn_list);
        if (thrd_create(&ramdev->worker, worker_thread, ramdev) != thrd_success) {
            goto fail_cvar_free;
        }

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = ramdev->name,
            .ctx = ramdev,
            .ops = &ramdisk_instance_proto,
            .proto_id = ZX_PROTOCOL_BLOCK_CORE,
            .proto_ops = &ramdisk_block_ops,
        };

        if ((status = device_add(ramctl->zxdev, &args, &ramdev->zxdev)) != ZX_OK) {
            ramdisk_release(ramdev);
            return status;
        }
        strcpy(reply, ramdev->name);
        *out_actual = strlen(reply);
        return ZX_OK;

fail_cvar_free:
        cnd_destroy(&ramdev->work_cvar);
fail_unmap:
        zx_vmar_unmap(zx_vmar_root_self(), ramdev->mapped_addr, sizebytes(ramdev));
fail_close_vmo:
        zx_handle_close(ramdev->vmo);
fail:
        free(ramdev);
        return status;
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
