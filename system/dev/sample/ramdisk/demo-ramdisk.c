// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <zircon/device/block.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#define MAX_TRANSFER_BYTES  (1 << 19)
#define BLOCK_SIZE          (1 << 9)    // 4k breaks their tests
#define BLOCK_COUNT         (1 << 12)
#define RAMDISK_SIZE        (BLOCK_SIZE * BLOCK_COUNT)

typedef struct ramdisk_device {
    zx_device_t*    zxdev;
    uintptr_t       mapped_addr;
    uint32_t        flags;
    zx_handle_t     vmo;
    bool            dead;
} ramdisk_device_t;

static void ramdisk_get_info(void* ctx, block_info_t* info) {
    ramdisk_device_t* ramdev = ctx;
    memset(info, 0, sizeof(*info));
    info->block_size = BLOCK_SIZE;
    info->block_count = BLOCK_COUNT;
    // Arbitrarily set, but matches the SATA driver for testing
    info->max_transfer_size = BLOCK_MAX_TRANSFER_UNBOUNDED;
    info->flags = ramdev->flags;
}

static zx_status_t ramdisk_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmd_len,
                                 void* reply, size_t max, size_t* out_actual) {
    ramdisk_device_t* ramdev = ctx;

    switch (op) {
    case IOCTL_BLOCK_GET_NAME: {
        strcpy(reply, "demo-ramdisk");
        *out_actual = strlen(reply);
        return ZX_OK;
    }

    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reply;
        if (max < sizeof(*info)) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
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

static void ramdisk_queue(void* ctx, block_op_t* bop, block_impl_queue_callback completion_cb,
                          void* cookie) {
    ramdisk_device_t* ramdev = ctx;

    if (ramdev->dead) {
        completion_cb(cookie, ZX_ERR_IO_NOT_PRESENT, bop);
        return;
    }

    switch ((bop->command &= BLOCK_OP_MASK)) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
        // perform validation common for both
        if ((bop->rw.offset_dev >= BLOCK_COUNT)
            || ((BLOCK_COUNT - bop->rw.offset_dev) < bop->rw.length)) {
            completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, bop);
            return;
        }

        void* addr = (void*) ramdev->mapped_addr + bop->rw.offset_dev * BLOCK_SIZE;
        zx_status_t status;

        // now perform actions specific to each
        if (bop->command == BLOCK_OP_READ) {
            status = zx_vmo_write(bop->rw.vmo, addr, bop->rw.offset_vmo * BLOCK_SIZE,
                                  bop->rw.length * BLOCK_SIZE);
        } else {
            status = zx_vmo_read(bop->rw.vmo, addr, bop->rw.offset_vmo * BLOCK_SIZE,
                                 bop->rw.length * BLOCK_SIZE);
        }
        completion_cb(cookie, status, bop);
        break;
        }

    case BLOCK_OP_FLUSH:
        completion_cb(cookie, ZX_OK, bop);
        break;

    default:
        completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, bop);
        break;
    }
}

static void ramdisk_query(void* ctx, block_info_t* bi, size_t* bopsz) {
    ramdisk_get_info(ctx, bi);
    *bopsz = sizeof(block_op_t);
}

static zx_off_t ramdisk_getsize(void* ctx) {
    return RAMDISK_SIZE;
}

static void ramdisk_unbind(void* ctx) {
    ramdisk_device_t* ramdev = ctx;
    ramdev->dead = true;
    device_remove(ramdev->zxdev);
}

static void ramdisk_release(void* ctx) {
    ramdisk_device_t* ramdev = ctx;

    if (ramdev->vmo != ZX_HANDLE_INVALID) {
        zx_vmar_unmap(zx_vmar_root_self(), ramdev->mapped_addr, RAMDISK_SIZE);
        zx_handle_close(ramdev->vmo);
    }
    free(ramdev);
}

static block_impl_protocol_ops_t block_ops = {
    .query = ramdisk_query,
    .queue = ramdisk_queue,
};

static zx_protocol_device_t ramdisk_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = ramdisk_ioctl,
    .get_size = ramdisk_getsize,
    .unbind = ramdisk_unbind,
    .release = ramdisk_release,
};

static zx_status_t ramdisk_driver_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status = ZX_OK;

    ramdisk_device_t* ramdev = calloc(1, sizeof((*ramdev)));
    if (ramdev == NULL) {
        return ZX_ERR_NO_MEMORY;
    }

    status = zx_vmo_create(RAMDISK_SIZE, 0, &ramdev->vmo);
    if (status != ZX_OK) {
        goto cleanup;
    }

    status = zx_vmar_map(zx_vmar_root_self(), ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                         0, ramdev->vmo, 0, RAMDISK_SIZE, &ramdev->mapped_addr);
    if (status != ZX_OK) {
        goto cleanup;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "demo-ramdisk",
        .ctx = ramdev,
        .ops = &ramdisk_proto,
        .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
        .proto_ops = &block_ops,
    };

    if ((status = device_add(parent, &args, &ramdev->zxdev)) != ZX_OK) {
        ramdisk_release(ramdev);
    }
    return status;

cleanup:
    zx_handle_close(ramdev->vmo);
    free(ramdev);
    return status;
}

static zx_driver_ops_t ramdisk_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ramdisk_driver_bind,
};

ZIRCON_DRIVER_BEGIN(ramdisk, ramdisk_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(ramdisk)
