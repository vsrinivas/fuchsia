// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <pretty/hexdump.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <lib/sync/completion.h>
#include <sys/param.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

#include "sata.h"

#define sata_devinfo_u32(base, offs) (((uint32_t)(base)[(offs) + 1] << 16) | ((uint32_t)(base)[(offs)]))
#define sata_devinfo_u64(base, offs) (((uint64_t)(base)[(offs) + 3] << 48) | ((uint64_t)(base)[(offs) + 2] << 32) | ((uint64_t)(base)[(offs) + 1] << 16) | ((uint32_t)(base)[(offs)]))

#define SATA_FLAG_DMA   (1 << 0)
#define SATA_FLAG_LBA48 (1 << 1)

typedef struct sata_device {
    zx_device_t* zxdev;
    ahci_device_t* controller;

    block_info_t info;

    int port;
    int flags;
    int max_cmd; // inclusive
} sata_device_t;

static void sata_device_identify_complete(block_op_t* op, zx_status_t status) {
    sata_txn_t* txn = containerof(op, sata_txn_t, bop);
    txn->status = status;
    sync_completion_signal((sync_completion_t*)op->cookie);
}

#define QEMU_MODEL_ID    "EQUMH RADDSI K" // "QEMU HARDDISK"
#define QEMU_SG_MAX      1024             // Linux kernel limit

static bool model_id_is_qemu(char* model_id) {
    return !memcmp(model_id, QEMU_MODEL_ID, sizeof(QEMU_MODEL_ID)-1);
}

static zx_status_t sata_device_identify(sata_device_t* dev, ahci_device_t* controller,
                                        const char* name) {
    // Set default devinfo
    sata_devinfo_t di = {
        .block_size = 512,
        .max_cmd = 1,
    };
    ahci_set_devinfo(controller, dev->port, &di);

    // send IDENTIFY DEVICE
    zx_handle_t vmo;
    zx_status_t status = zx_vmo_create(512, 0, &vmo);
    if (status != ZX_OK) {
        zxlogf(TRACE, "sata: error %d allocating vmo\n", status);
        return status;
    }

    sync_completion_t completion = SYNC_COMPLETION_INIT;
    sata_txn_t txn = {
        .bop = {
            .rw.vmo = vmo,
            .rw.length = 1,
            .rw.offset_dev = 0,
            .rw.offset_vmo = 0,
            .rw.pages = NULL,
            .completion_cb = sata_device_identify_complete,
            .cookie = &completion,
        },
        .cmd = SATA_CMD_IDENTIFY_DEVICE,
        .device = 0,
    };

    ahci_queue(controller, dev->port, &txn);
    sync_completion_wait(&completion, ZX_TIME_INFINITE);

    if (txn.status != ZX_OK) {
        zxlogf(ERROR, "%s: error %d in device identify\n", name, txn.status);
        return txn.status;
    }

    // parse results
    int flags = 0;
    uint16_t devinfo[512 / sizeof(uint16_t)];
    status = zx_vmo_read(vmo, devinfo, 0, sizeof(devinfo));
    if (status != ZX_OK) {
        zxlogf(ERROR, "sata: error %d in vmo_read\n", status);
        return ZX_ERR_INTERNAL;
    }
    zx_handle_close(vmo);

    char str[41]; // model id is 40 chars
    zxlogf(INFO, "%s: dev info\n", name);
    snprintf(str, SATA_DEVINFO_SERIAL_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_SERIAL));
    zxlogf(INFO, "  serial=%s\n", str);
    snprintf(str, SATA_DEVINFO_FW_REV_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_FW_REV));
    zxlogf(INFO, "  firmware rev=%s\n", str);
    snprintf(str, SATA_DEVINFO_MODEL_ID_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_MODEL_ID));
    zxlogf(INFO, "  model id=%s\n", str);

    bool is_qemu = model_id_is_qemu((char*)(devinfo + SATA_DEVINFO_MODEL_ID));

    uint16_t major = *(devinfo + SATA_DEVINFO_MAJOR_VERS);
    zxlogf(INFO, "  major=0x%x ", major);
    switch (32 - __builtin_clz(major) - 1) {
        case 10:
            zxlogf(INFO, "ACS3");
            break;
        case 9:
            zxlogf(INFO, "ACS2");
            break;
        case 8:
            zxlogf(INFO, "ATA8-ACS");
            break;
        case 7:
        case 6:
        case 5:
            zxlogf(INFO, "ATA/ATAPI");
            break;
        default:
            zxlogf(INFO, "Obsolete");
            break;
    }

    uint16_t cap = *(devinfo + SATA_DEVINFO_CAP);
    if (cap & (1 << 8)) {
        zxlogf(INFO, " DMA");
        flags |= SATA_FLAG_DMA;
    } else {
        zxlogf(INFO, " PIO");
    }
    dev->max_cmd = *(devinfo + SATA_DEVINFO_QUEUE_DEPTH);
    zxlogf(INFO, " %d commands\n", dev->max_cmd + 1);

    uint32_t block_size = 512; // default
    uint64_t block_count = 0;
    if (cap & (1 << 9)) {
        if ((*(devinfo + SATA_DEVINFO_SECTOR_SIZE) & 0xd000) == 0x5000) {
            block_size = 2 * sata_devinfo_u32(devinfo, SATA_DEVINFO_LOGICAL_SECTOR_SIZE);
        }
        if (*(devinfo + SATA_DEVINFO_CMD_SET_2) & (1 << 10)) {
            flags |= SATA_FLAG_LBA48;
            block_count = sata_devinfo_u64(devinfo, SATA_DEVINFO_LBA_CAPACITY_2);
            zxlogf(INFO, "  LBA48");
        } else {
            block_count = sata_devinfo_u32(devinfo, SATA_DEVINFO_LBA_CAPACITY);
            zxlogf(INFO, "  LBA");
        }
        zxlogf(INFO, " %" PRIu64 " sectors,  sector size=%u\n", block_count, block_size);
    } else {
        zxlogf(INFO, "  CHS unsupported!\n");
    }
    dev->flags = flags;

    memset(&dev->info, 0, sizeof(dev->info));
    dev->info.block_size = block_size;
    dev->info.block_count = block_count;

    uint32_t max_sg_size = SATA_MAX_BLOCK_COUNT * block_size; // SATA cmd limit
    if (is_qemu) {
        max_sg_size = MIN(max_sg_size, QEMU_SG_MAX * block_size);
    }
    dev->info.max_transfer_size = MIN(AHCI_MAX_BYTES, max_sg_size);

    // set devinfo on controller
    di.block_size = block_size,
    di.max_cmd = dev->max_cmd,

    ahci_set_devinfo(controller, dev->port, &di);

    return ZX_OK;
}

// implement device protocol:

static zx_protocol_device_t sata_device_proto;

static zx_status_t sata_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen, void* reply,
                              size_t max, size_t* out_actual) {
    sata_device_t* device = ctx;
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reply;
        if (max < sizeof(*info))
            return ZX_ERR_BUFFER_TOO_SMALL;
        memcpy(info, &device->info, sizeof(*info));
        *out_actual = sizeof(*info);
        return ZX_OK;
    }
    case IOCTL_DEVICE_SYNC: {
        zxlogf(TRACE, "sata: IOCTL_DEVICE_SYNC\n");
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_off_t sata_getsize(void* ctx) {
    sata_device_t* device = ctx;
    return device->info.block_count * device->info.block_size;
}

static void sata_release(void* ctx) {
    sata_device_t* device = ctx;
    free(device);
}

static zx_protocol_device_t sata_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = sata_ioctl,
    .get_size = sata_getsize,
    .release = sata_release,
};

static void sata_query(void* ctx, block_info_t* info_out, size_t* block_op_size_out) {
    sata_device_t* dev = ctx;
    memcpy(info_out, &dev->info, sizeof(*info_out));
    *block_op_size_out = sizeof(sata_txn_t);
}

static void sata_queue(void* ctx, block_op_t* bop) {
    sata_device_t* dev = ctx;
    sata_txn_t* txn = containerof(bop, sata_txn_t, bop);

    switch (BLOCK_OP(bop->command)) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE:
        // complete empty transactions immediately
        if (bop->rw.length == 0) {
            block_complete(bop, ZX_ERR_INVALID_ARGS);
            return;
        }
        // transaction must fit within device
        if ((bop->rw.offset_dev >= dev->info.block_count) ||
            ((dev->info.block_count - bop->rw.offset_dev) < bop->rw.length)) {
            block_complete(bop, ZX_ERR_OUT_OF_RANGE);
            return;
        }

        txn->cmd = (BLOCK_OP(bop->command) == BLOCK_OP_READ) ?
                   SATA_CMD_READ_DMA_EXT : SATA_CMD_WRITE_DMA_EXT;
        txn->device = 0x40;
        zxlogf(TRACE, "sata: queue op 0x%x txn %p\n", bop->command, txn);
        break;
    case BLOCK_OP_FLUSH:
        zxlogf(TRACE, "sata: queue FLUSH txn %p\n", txn);
        break;
    default:
        block_complete(bop, ZX_ERR_NOT_SUPPORTED);
        return;
    }

    ahci_queue(dev->controller, dev->port, txn);
}

static block_protocol_ops_t sata_block_proto = {
    .query = sata_query,
    .queue = sata_queue,
};

zx_status_t sata_bind(ahci_device_t* controller, zx_device_t* parent, int port) {
    // initialize the device
    sata_device_t* device = calloc(1, sizeof(sata_device_t));
    if (!device) {
        zxlogf(ERROR, "sata: out of memory\n");
        return ZX_ERR_NO_MEMORY;
    }
    device->controller = controller;

    device->port = port;

    char name[8];
    snprintf(name, sizeof(name), "sata%d", port);

    // send device identify
    zx_status_t status = sata_device_identify(device, controller, name);
    if (status < 0) {
        free(device);
        return status;
    }

    // add the device
    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = device,
        .ops = &sata_device_proto,
        .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
        .proto_ops = &sata_block_proto,
    };

    status = device_add(parent, &args, &device->zxdev);
    if (status < 0) {
        free(device);
        return status;
    }

    return ZX_OK;
}
