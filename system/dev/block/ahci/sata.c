// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>

#include <zircon/types.h>
#include <pretty/hexdump.h>
#include <sync/completion.h>
#include <sys/param.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "sata.h"

#define sata_devinfo_u32(base, offs) (((uint32_t)(base)[(offs) + 1] << 16) | ((uint32_t)(base)[(offs)]))
#define sata_devinfo_u64(base, offs) (((uint64_t)(base)[(offs) + 3] << 48) | ((uint64_t)(base)[(offs) + 2] << 32) | ((uint64_t)(base)[(offs) + 1] << 16) | ((uint32_t)(base)[(offs)]))

#define SATA_FLAG_DMA   (1 << 0)
#define SATA_FLAG_LBA48 (1 << 1)

typedef struct sata_device {
    zx_device_t* zxdev;
    zx_device_t* parent;

    block_callbacks_t* callbacks;
    block_info_t info;

    int port;
    int flags;
    int max_cmd; // inclusive

    size_t sector_sz;
    zx_off_t capacity; // bytes
} sata_device_t;

static void sata_device_identify_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

static zx_status_t sata_device_identify(sata_device_t* dev, zx_device_t* controller, const char* name) {
    // send IDENTIFY DEVICE
    iotxn_t* txn;
    zx_status_t status = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS, 512);
    if (status != ZX_OK) {
        dprintf(ERROR, "%s: error %d allocating iotxn\n", name, status);
        return status;
    }

    completion_t completion = COMPLETION_INIT;

    sata_pdata_t* pdata = sata_iotxn_pdata(txn);
    pdata->cmd = SATA_CMD_IDENTIFY_DEVICE;
    pdata->device = 0;
    pdata->max_cmd = dev->max_cmd;
    pdata->port = dev->port;
    txn->complete_cb = sata_device_identify_complete;
    txn->cookie = &completion;
    txn->length = 512;

    iotxn_queue(controller, txn);
    completion_wait(&completion, ZX_TIME_INFINITE);

    if (txn->status != ZX_OK) {
        dprintf(ERROR, "%s: error %d in device identify\n", name, txn->status);
        return txn->status;
    }
    assert(txn->actual == 512);

    // parse results
    int flags = 0;
    uint16_t devinfo[512 / sizeof(uint16_t)];
    iotxn_copyfrom(txn, devinfo, 512, 0);
    iotxn_release(txn);

    char str[41]; // model id is 40 chars
    dprintf(INFO, "%s: dev info\n", name);
    snprintf(str, SATA_DEVINFO_SERIAL_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_SERIAL));
    dprintf(INFO, "  serial=%s\n", str);
    snprintf(str, SATA_DEVINFO_FW_REV_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_FW_REV));
    dprintf(INFO, "  firmware rev=%s\n", str);
    snprintf(str, SATA_DEVINFO_MODEL_ID_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_MODEL_ID));
    dprintf(INFO, "  model id=%s\n", str);

    uint16_t major = *(devinfo + SATA_DEVINFO_MAJOR_VERS);
    dprintf(INFO, "  major=0x%x ", major);
    switch (32 - __builtin_clz(major) - 1) {
        case 10:
            dprintf(INFO, "ACS3");
            break;
        case 9:
            dprintf(INFO, "ACS2");
            break;
        case 8:
            dprintf(INFO, "ATA8-ACS");
            break;
        case 7:
        case 6:
        case 5:
            dprintf(INFO, "ATA/ATAPI");
            break;
        default:
            dprintf(INFO, "Obsolete");
            break;
    }

    uint16_t cap = *(devinfo + SATA_DEVINFO_CAP);
    if (cap & (1 << 8)) {
        dprintf(INFO, " DMA");
        flags |= SATA_FLAG_DMA;
    } else {
        dprintf(INFO, " PIO");
    }
    dev->max_cmd = *(devinfo + SATA_DEVINFO_QUEUE_DEPTH);
    dprintf(INFO, " %d commands\n", dev->max_cmd + 1);
    if (cap & (1 << 9)) {
        dev->sector_sz = 512; // default
        if ((*(devinfo + SATA_DEVINFO_SECTOR_SIZE) & 0xd000) == 0x5000) {
            dev->sector_sz = 2 * sata_devinfo_u32(devinfo, SATA_DEVINFO_LOGICAL_SECTOR_SIZE);
        }
        if (*(devinfo + SATA_DEVINFO_CMD_SET_2) & (1 << 10)) {
            flags |= SATA_FLAG_LBA48;
            dev->capacity = sata_devinfo_u64(devinfo, SATA_DEVINFO_LBA_CAPACITY_2) * dev->sector_sz;
            dprintf(INFO, "  LBA48");
        } else {
            dev->capacity = sata_devinfo_u32(devinfo, SATA_DEVINFO_LBA_CAPACITY) * dev->sector_sz;
            dprintf(INFO, "  LBA");
        }
        dprintf(INFO, " %" PRIu64 " sectors, size=%zu\n", dev->capacity, dev->sector_sz);
    } else {
        dprintf(INFO, "  CHS unsupported!\n");
    }
    dev->flags = flags;

    memset(&dev->info, 0, sizeof(dev->info));
    dev->info.block_size = dev->sector_sz;
    dev->info.block_count = dev->capacity / dev->sector_sz;
    dev->info.max_transfer_size = MIN(AHCI_MAX_PRDS * PAGE_SIZE, // fully discontiguous
                                      SATA_MAX_BLOCK_COUNT * dev->sector_sz); // SATA cmd limit

    return ZX_OK;
}

// implement device protocol:

static zx_protocol_device_t sata_device_proto;

static void sata_iotxn_queue(void* ctx, iotxn_t* txn) {
    sata_device_t* device = ctx;

    // offset must be aligned to block size
    if (txn->offset % device->sector_sz) {
        iotxn_complete(txn, ZX_ERR_INVALID_ARGS, 0);
        return;
    }

    // length must be a multiple of block size
    if (txn->length % device->sector_sz) {
        iotxn_complete(txn, ZX_ERR_INVALID_ARGS, 0);
        return;
    }

    // constrain to device capacity
    txn->length = MIN(txn->length, device->capacity - txn->offset);

    // transfer must be smaller than max size
    if (txn->length > device->info.max_transfer_size) {
        iotxn_complete(txn, ZX_ERR_OUT_OF_RANGE, 0);
        return;
    }

    sata_pdata_t* pdata = sata_iotxn_pdata(txn);
    pdata->cmd = txn->opcode == IOTXN_OP_READ ? SATA_CMD_READ_DMA_EXT : SATA_CMD_WRITE_DMA_EXT;
    pdata->device = 0x40;
    pdata->lba = txn->offset / device->sector_sz;
    pdata->count = txn->length / device->sector_sz;
    pdata->max_cmd = device->max_cmd;
    pdata->port = device->port;

    iotxn_queue(device->parent, txn);
}

static void sata_sync_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

static void sata_get_info(sata_device_t* dev, block_info_t* info) {
    memcpy(info, &dev->info, sizeof(*info));
}

static zx_status_t sata_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen, void* reply,
                              size_t max, size_t* out_actual) {
    sata_device_t* device = ctx;
    // TODO implement other block ioctls
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reply;
        if (max < sizeof(*info))
            return ZX_ERR_BUFFER_TOO_SMALL;
        sata_get_info(device, info);
        *out_actual = sizeof(*info);
        return ZX_OK;
    }
    case IOCTL_BLOCK_RR_PART: {
        // rebind to reread the partition table
        return device_rebind(device->zxdev);
    }
    case IOCTL_DEVICE_SYNC: {
        iotxn_t* txn;
        zx_status_t status = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS, 0);
        if (status != ZX_OK) {
            return status;
        }
        completion_t completion = COMPLETION_INIT;
        txn->opcode = IOTXN_OP_READ;
        txn->flags = IOTXN_SYNC_BEFORE;
        txn->offset = 0;
        txn->length = 0;
        txn->complete_cb = sata_sync_complete;
        txn->cookie = &completion;
        iotxn_queue(device->zxdev, txn);
        completion_wait(&completion, ZX_TIME_INFINITE);
        status = txn->status;
        iotxn_release(txn);
        return status;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_off_t sata_getsize(void* ctx) {
    sata_device_t* device = ctx;
    return device->capacity;
}

static void sata_release(void* ctx) {
    sata_device_t* device = ctx;
    free(device);
}

static zx_protocol_device_t sata_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = sata_ioctl,
    .iotxn_queue = sata_iotxn_queue,
    .get_size = sata_getsize,
    .release = sata_release,
};

static void sata_block_set_callbacks(void* ctx, block_callbacks_t* cb) {
    sata_device_t* device = ctx;
    device->callbacks = cb;
}

static void sata_block_get_info(void* ctx, block_info_t* info) {
    sata_device_t* device = ctx;
    sata_get_info(device, info);
}

static void sata_block_complete(iotxn_t* txn, void* cookie) {
    sata_device_t* dev;
    memcpy(&dev, txn->extra, sizeof(sata_device_t*));
    dprintf(SPEW, "sata: fifo_complete dev %p cookie %p callbacks %p status %d actual 0x%"
            PRIx64 "\n", dev, cookie, dev->callbacks, txn->status, txn->actual);
    dev->callbacks->complete(cookie, txn->status);
    iotxn_release(txn);
}

static void sata_block_txn(sata_device_t* dev, uint32_t opcode, zx_handle_t vmo,
                           uint64_t length, uint64_t vmo_offset, uint64_t dev_offset,
                           void* cookie) {
    if ((dev_offset % dev->sector_sz) || (length % dev->sector_sz)) {
        dev->callbacks->complete(cookie, ZX_ERR_INVALID_ARGS);
        return;
    }
    if ((dev_offset >= dev->capacity) || (length >= (dev->capacity - dev_offset))) {
        dev->callbacks->complete(cookie, ZX_ERR_OUT_OF_RANGE);
        return;
    }

    zx_status_t status;
    iotxn_t* txn;
    if ((status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, vmo, vmo_offset, length)) != ZX_OK) {
        dev->callbacks->complete(cookie, status);
        return;
    }
    txn->opcode = opcode;
    txn->offset = dev_offset;
    txn->complete_cb = sata_block_complete;
    txn->cookie = cookie;
    memcpy(txn->extra, &dev, sizeof(sata_device_t*));

    iotxn_queue(dev->zxdev, txn);
}

static void sata_block_read(void* ctx, zx_handle_t vmo, uint64_t length,
                           uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    sata_block_txn(ctx, IOTXN_OP_READ, vmo, length, vmo_offset, dev_offset, cookie);
}

static void sata_block_write(void* ctx, zx_handle_t vmo, uint64_t length,
                            uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    sata_block_txn(ctx, IOTXN_OP_WRITE, vmo, length, vmo_offset, dev_offset, cookie);
}

static block_protocol_ops_t sata_block_ops = {
    .set_callbacks = sata_block_set_callbacks,
    .get_info = sata_block_get_info,
    .read = sata_block_read,
    .write = sata_block_write,
};

zx_status_t sata_bind(zx_device_t* dev, int port) {
    // initialize the device
    sata_device_t* device = calloc(1, sizeof(sata_device_t));
    if (!device) {
        dprintf(ERROR, "sata: out of memory\n");
        return ZX_ERR_NO_MEMORY;
    }
    device->parent = dev;

    device->port = port;

    char name[8];
    snprintf(name, sizeof(name), "sata%d", port);

    // send device identify
    zx_status_t status = sata_device_identify(device, dev, name);
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
        .proto_id = ZX_PROTOCOL_BLOCK_CORE,
        .proto_ops = &sata_block_ops,
    };

    status = device_add(dev, &args, &device->zxdev);
    if (status < 0) {
        free(device);
        return status;
    }

    return ZX_OK;
}
