// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>

#include <hexdump/hexdump.h>
#include <magenta/types.h>
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

#define TRACE 1

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define sata_devinfo_u32(base, offs) (((uint32_t)(base)[(offs) + 1] << 16) | ((uint32_t)(base)[(offs)]))
#define sata_devinfo_u64(base, offs) (((uint64_t)(base)[(offs) + 3] << 48) | ((uint64_t)(base)[(offs) + 2] << 32) | ((uint64_t)(base)[(offs) + 1] << 16) | ((uint32_t)(base)[(offs)]))

#define SATA_FLAG_DMA   (1 << 0)
#define SATA_FLAG_LBA48 (1 << 1)

typedef struct sata_device {
    mx_device_t device;

    block_callbacks_t* callbacks;

    int port;
    int flags;
    int max_cmd; // inclusive

    size_t sector_sz;
    mx_off_t capacity; // bytes
} sata_device_t;

#define get_sata_device(dev) containerof(dev, sata_device_t, device)

static void sata_device_identify_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

static mx_status_t sata_device_identify(sata_device_t* dev, mx_device_t* controller) {
    // send IDENTIFY DEVICE
    iotxn_t* txn;
    mx_status_t status = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS, 512);
    if (status != NO_ERROR) {
        xprintf("%s: error %d allocating iotxn\n", dev->device.name, status);
        return status;
    }

    completion_t completion = COMPLETION_INIT;

    sata_pdata_t* pdata = sata_iotxn_pdata(txn);
    pdata->cmd = SATA_CMD_IDENTIFY_DEVICE;
    pdata->device = 0;
    pdata->max_cmd = dev->max_cmd;
    pdata->port = dev->port;
    txn->protocol = MX_PROTOCOL_SATA;
    txn->complete_cb = sata_device_identify_complete;
    txn->cookie = &completion;
    txn->length = 512;

    ahci_iotxn_queue(controller, txn);
    completion_wait(&completion, MX_TIME_INFINITE);

    if (txn->status != NO_ERROR) {
        xprintf("%s: error %d in device identify\n", dev->device.name, txn->status);
        return txn->status;
    }
    assert(txn->actual == 512);

    // parse results
    int flags = 0;
    uint16_t devinfo[512 / sizeof(uint16_t)];
    iotxn_copyfrom(txn, devinfo, 512, 0);
    iotxn_release(txn);

    char str[41]; // model id is 40 chars
    xprintf("%s: dev info\n", dev->device.name);
    snprintf(str, SATA_DEVINFO_SERIAL_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_SERIAL));
    xprintf("  serial=%s\n", str);
    snprintf(str, SATA_DEVINFO_FW_REV_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_FW_REV));
    xprintf("  firmware rev=%s\n", str);
    snprintf(str, SATA_DEVINFO_MODEL_ID_LEN + 1, "%s", (char*)(devinfo + SATA_DEVINFO_MODEL_ID));
    xprintf("  model id=%s\n", str);

    uint16_t major = *(devinfo + SATA_DEVINFO_MAJOR_VERS);
    xprintf("  major=0x%x ", major);
    switch (32 - __builtin_clz(major) - 1) {
        case 10:
            xprintf("ACS3");
            break;
        case 9:
            xprintf("ACS2");
            break;
        case 8:
            xprintf("ATA8-ACS");
            break;
        case 7:
        case 6:
        case 5:
            xprintf("ATA/ATAPI");
            break;
        default:
            xprintf("Obsolete");
            break;
    }

    uint16_t cap = *(devinfo + SATA_DEVINFO_CAP);
    if (cap & (1 << 8)) {
        xprintf(" DMA");
        flags |= SATA_FLAG_DMA;
    } else {
        xprintf(" PIO");
    }
    dev->max_cmd = *(devinfo + SATA_DEVINFO_QUEUE_DEPTH);
    xprintf(" %d commands\n", dev->max_cmd + 1);
    if (cap & (1 << 9)) {
        dev->sector_sz = 512; // default
        if ((*(devinfo + SATA_DEVINFO_SECTOR_SIZE) & 0xd000) == 0x5000) {
            dev->sector_sz = 2 * sata_devinfo_u32(devinfo, SATA_DEVINFO_LOGICAL_SECTOR_SIZE);
        }
        if (*(devinfo + SATA_DEVINFO_CMD_SET_2) & (1 << 10)) {
            flags |= SATA_FLAG_LBA48;
            dev->capacity = sata_devinfo_u64(devinfo, SATA_DEVINFO_LBA_CAPACITY_2) * dev->sector_sz;
            xprintf("  LBA48");
        } else {
            dev->capacity = sata_devinfo_u32(devinfo, SATA_DEVINFO_LBA_CAPACITY) * dev->sector_sz;
            xprintf("  LBA");
        }
        xprintf(" %" PRIu64 " sectors, size=%zu\n", dev->capacity, dev->sector_sz);
    } else {
        xprintf("  CHS unsupported!\n");
    }
    dev->flags = flags;

    return NO_ERROR;
}

// implement device protocol:

static mx_protocol_device_t sata_device_proto;

static void sata_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    sata_device_t* device = get_sata_device(dev);

    // offset must be aligned to block size
    if (txn->offset % device->sector_sz) {
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    // length must be a multiple of block size
    if (txn->length % device->sector_sz) {
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    // constrain to device capacity
    txn->length = MIN(txn->length, device->capacity - txn->offset);

    sata_pdata_t* pdata = sata_iotxn_pdata(txn);
    pdata->cmd = txn->opcode == IOTXN_OP_READ ? SATA_CMD_READ_DMA_EXT : SATA_CMD_WRITE_DMA_EXT;
    pdata->device = 0x40;
    pdata->lba = txn->offset / device->sector_sz;
    pdata->count = txn->length / device->sector_sz;
    pdata->max_cmd = device->max_cmd;
    pdata->port = device->port;

    ahci_iotxn_queue(dev->parent, txn);
}

static void sata_sync_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

static ssize_t sata_ioctl(mx_device_t* dev, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max) {
    sata_device_t* device = get_sata_device(dev);
    // TODO implement other block ioctls
    switch (op) {
    case IOCTL_BLOCK_GET_SIZE: {
        uint64_t* size = reply;
        if (max < sizeof(*size)) return ERR_BUFFER_TOO_SMALL;
        *size = device->capacity;
        return sizeof(*size);
    }
    case IOCTL_BLOCK_GET_BLOCKSIZE: {
        uint64_t* blksize = reply;
        if (max < sizeof(*blksize)) return ERR_BUFFER_TOO_SMALL;
        *blksize = device->sector_sz;
        return sizeof(*blksize);
    }
    case IOCTL_BLOCK_RR_PART: {
        // rebind to reread the partition table
        return device_rebind(dev);
    }
    case IOCTL_DEVICE_SYNC: {
        iotxn_t* txn;
        mx_status_t status = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS, 0);
        if (status != NO_ERROR) {
            return status;
        }
        completion_t completion = COMPLETION_INIT;
        txn->opcode = IOTXN_OP_READ;
        txn->flags = IOTXN_SYNC_BEFORE;
        txn->offset = 0;
        txn->length = 0;
        txn->complete_cb = sata_sync_complete;
        txn->cookie = &completion;
        sata_iotxn_queue(dev, txn);
        completion_wait(&completion, MX_TIME_INFINITE);
        status = txn->status;
        iotxn_release(txn);
        return status;
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_off_t sata_getsize(mx_device_t* dev) {
    sata_device_t* device = get_sata_device(dev);
    return device->capacity;
}

static mx_status_t sata_release(mx_device_t* dev) {
    sata_device_t* device = get_sata_device(dev);
    free(device);
    return NO_ERROR;
}

static mx_protocol_device_t sata_device_proto = {
    .ioctl = sata_ioctl,
    .iotxn_queue = sata_iotxn_queue,
    .get_size = sata_getsize,
    .release = sata_release,
};

static void sata_fifo_set_callbacks(mx_device_t* dev, block_callbacks_t* cb) {
    sata_device_t* device = get_sata_device(dev);
    device->callbacks = cb;
};

static void sata_fifo_complete(iotxn_t* txn, void* cookie) {
    sata_device_t* dev;
    memcpy(&dev, txn->extra, sizeof(sata_device_t*));
    //xprintf("sata: fifo_complete dev %p cookie %p callbacks %p status %d actual 0x%" PRIx64 "\n", dev, cookie, dev->callbacks, txn->status, txn->actual);
    dev->callbacks->complete(cookie, txn->status);
    iotxn_release(txn);
}

static void sata_fifo_read(mx_device_t* dev, mx_handle_t vmo, uint64_t length,
                           uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    sata_device_t* device = get_sata_device(dev);
    //xprintf("sata: fifo_read dev %p vmo_offset 0x%" PRIx64 " dev_offset 0x%" PRIx64 " length 0x%" PRIx64 "\n", dev, vmo_offset, dev_offset, length);

    mx_status_t status;
    iotxn_t* txn;
    if ((status = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS, 0)) != NO_ERROR) {
        device->callbacks->complete(cookie, status);
        return;
    }

    txn->vmo_handle = vmo;
    txn->vmo_offset = vmo_offset;
    txn->vmo_length = length;

    txn->opcode = IOTXN_OP_READ;
    txn->offset = dev_offset;
    txn->length = length;
    txn->complete_cb = sata_fifo_complete;
    txn->cookie = cookie;
    memcpy(txn->extra, &device, sizeof(sata_device_t*));

    sata_iotxn_queue(dev, txn);
}

static void sata_fifo_write(mx_device_t* dev, mx_handle_t vmo, uint64_t length,
                            uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    sata_device_t* device = get_sata_device(dev);
    //xprintf("sata: fifo_write dev %p vmo_offset 0x%" PRIx64 " dev_offset 0x%" PRIx64 " length 0x%" PRIx64 "\n", dev, vmo_offset, dev_offset, length);

    mx_status_t status;
    iotxn_t* txn;
    if ((status = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS, 0)) != NO_ERROR) {
        device->callbacks->complete(cookie, status);
        return;
    }

    txn->vmo_handle = vmo;
    txn->vmo_offset = vmo_offset;
    txn->vmo_length = length;

    txn->opcode = IOTXN_OP_WRITE;
    txn->offset = dev_offset;
    txn->length = length;
    txn->complete_cb = sata_fifo_complete;
    txn->cookie = cookie;
    memcpy(txn->extra, &device, sizeof(sata_device_t*));

    sata_iotxn_queue(dev, txn);
}

static block_ops_t sata_block_ops = {
    .set_callbacks = sata_fifo_set_callbacks,
    .read = sata_fifo_read,
    .write = sata_fifo_write,
};

mx_status_t sata_bind(mx_device_t* dev, int port) {
    // initialize the device
    sata_device_t* device = calloc(1, sizeof(sata_device_t));
    if (!device) {
        xprintf("sata: out of memory\n");
        return ERR_NO_MEMORY;
    }

    char name[8];
    snprintf(name, sizeof(name), "sata%d", port);
    device_init(&device->device, dev->driver, name, &sata_device_proto);

    device->port = port;

    // send device identify
    mx_status_t status = sata_device_identify(device, dev);
    if (status < 0) {
        free(device);
        return status;
    }

    // add the device
    device->device.protocol_id = MX_PROTOCOL_BLOCK_CORE;
    device->device.protocol_ops = &sata_block_ops;
    device_add(&device->device, dev);

    return NO_ERROR;
}
