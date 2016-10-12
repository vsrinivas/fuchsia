// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <magenta/listnode.h>
#include <sys/param.h>
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <threads.h>

#include "gpt.h"

#define TRACE 1

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define TXN_SIZE 0x4000 // 128 partition entries

typedef struct gptpart_device {
    mx_device_t device;
    gpt_entry_t gpt_entry;
    uint64_t blksize;
} gptpart_device_t;

#define get_gptpart_device(dev) containerof(dev, gptpart_device_t, device)

struct guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

static void uint8_to_guid_string(char* dst, uint8_t* src) {
    struct guid* guid = (struct guid*)src;
    sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2, guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3], guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

static void utf16_to_cstring(char* dst, uint8_t* src, size_t charcount) {
    while (charcount > 0) {
        *dst++ = *src;
        src += 2; // FIXME cheesy
        charcount -= 2;
    }
}

static uint64_t getsize(gptpart_device_t* dev) {
    // last LBA is inclusive
    uint64_t lbacount = dev->gpt_entry.last_lba - dev->gpt_entry.first_lba + 1;
    return lbacount * dev->blksize;
}

// implement device protocol:

static ssize_t gpt_ioctl(mx_device_t* dev, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max) {
    gptpart_device_t* device = get_gptpart_device(dev);
    switch (op) {
    case IOCTL_BLOCK_GET_SIZE: {
        uint64_t* size = reply;
        if (max < sizeof(*size)) return ERR_BUFFER_TOO_SMALL;
        *size = getsize(device);
        return sizeof(*size);
    }
    case IOCTL_BLOCK_GET_BLOCKSIZE: {
        uint64_t* blksize = reply;
        if (max < sizeof(*blksize)) return ERR_BUFFER_TOO_SMALL;
        *blksize = device->blksize;
        return sizeof(*blksize);
    }
    case IOCTL_BLOCK_GET_GUID: {
        char* guid = reply;
        if (max < GPT_GUID_STRLEN) return ERR_BUFFER_TOO_SMALL;
        uint8_to_guid_string(guid, device->gpt_entry.type);
        return GPT_GUID_STRLEN;
    }
    case IOCTL_BLOCK_GET_NAME: {
        char* name = reply;
        memset(name, 0, max);
        // save room for the null terminator
        utf16_to_cstring(name, device->gpt_entry.name, MIN((max - 1) * 2, GPT_NAME_LEN));
        return strnlen(name, GPT_NAME_LEN / 2);
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static void gpt_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {
    gptpart_device_t* device = get_gptpart_device(dev);
    // sanity check
    uint64_t off_lba = txn->offset / device->blksize;
    uint64_t first = device->gpt_entry.first_lba;
    uint64_t last = device->gpt_entry.last_lba;
    if (first + off_lba > last) {
        xprintf("%s: offset 0x%" PRIx64 " is past the end of partition!\n", dev->name, txn->offset);
        txn->ops->complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }
    // constrain if too many bytes are requested
    txn->length = MIN((last - (first + off_lba) + 1) * device->blksize, txn->length);
    // adjust offset
    txn->offset = first * device->blksize + txn->offset;
    iotxn_queue(dev->parent, txn);
}

static mx_off_t gpt_getsize(mx_device_t* dev) {
    return getsize(get_gptpart_device(dev));
}

static void gpt_unbind(mx_device_t* dev) {
    gptpart_device_t* device = get_gptpart_device(dev);
    device_remove(&device->device);
}

static mx_status_t gpt_release(mx_device_t* dev) {
    gptpart_device_t* device = get_gptpart_device(dev);
    free(device);
    return NO_ERROR;
}

static mx_protocol_device_t gpt_proto = {
    .ioctl = gpt_ioctl,
    .iotxn_queue = gpt_iotxn_queue,
    .get_size = gpt_getsize,
    .unbind = gpt_unbind,
    .release = gpt_release,
};

static void gpt_read_sync_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

typedef struct gpt_bind_info {
    mx_driver_t* drv;
    mx_device_t* dev;
} gpt_bind_info_t;

static int gpt_bind_thread(void* arg) {
    gpt_bind_info_t* info = (gpt_bind_info_t*)arg;
    mx_device_t* dev = info->dev;
    mx_driver_t* drv = info->drv;
    free(info);

    unsigned partitions = 0; // used to keep track of number of partitions found
    uint64_t blksize;
    ssize_t rc = dev->ops->ioctl(dev, IOCTL_BLOCK_GET_BLOCKSIZE, NULL, 0, &blksize, sizeof(blksize));
    if (rc < 0) {
        xprintf("gpt: Error %zd getting blksize for dev=%s\n", rc, dev->name);
        goto unbind;
    }

    // sanity check the default txn size with the block size
    if (TXN_SIZE % blksize) {
        xprintf("gpt: default txn size=%d is not aligned to blksize=%" PRIu64 "!\n", TXN_SIZE, blksize);
    }

    // allocate an iotxn to read the partition table
    iotxn_t* txn;
    mx_status_t status = iotxn_alloc(&txn, 0, TXN_SIZE, 0);
    if (status != NO_ERROR) {
        xprintf("gpt: error %d allocating iotxn\n", status);
        goto unbind;
    }

    completion_t completion = COMPLETION_INIT;

    // read partition table header synchronously (LBA1)
    txn->opcode = IOTXN_OP_READ;
    txn->offset = blksize;
    txn->length = blksize;
    txn->complete_cb = gpt_read_sync_complete;
    txn->cookie = &completion;

    iotxn_queue(dev, txn);
    completion_wait(&completion, MX_TIME_INFINITE);

    if (txn->status != NO_ERROR) {
        xprintf("gpt: error %d reading partition header\n", txn->status);
        goto unbind;
    }

    // read the header
    gpt_t header;
    txn->ops->copyfrom(txn, &header, sizeof(gpt_t), 0);
    if (header.magic != GPT_MAGIC) {
        xprintf("gpt: bad header magic\n");
        txn->ops->release(txn);
        goto unbind;
    }

    xprintf("gpt: found gpt header %u entries @ lba%" PRIu64 "\n", header.entries_count, header.entries);

    // read partition table entries
    size_t table_sz = header.entries_count * header.entries_sz;
    if (table_sz > TXN_SIZE) {
        xprintf("gpt: partition table is bigger than the iotxn!\n");
        // FIXME read the whole partition table. ok for now because on pixel2, this is
        // enough to read the entries that actually contain valid data
        table_sz = TXN_SIZE;
    }
    txn->opcode = IOTXN_OP_READ;
    txn->offset = header.entries * blksize;
    txn->length = table_sz;
    txn->complete_cb = gpt_read_sync_complete;
    txn->cookie = &completion;

    completion_reset(&completion);
    iotxn_queue(dev, txn);
    completion_wait(&completion, MX_TIME_INFINITE);

    for (partitions = 0; partitions < header.entries_count; partitions++) {
        if (partitions * header.entries_sz > txn->actual) break;

        gptpart_device_t* device = calloc(1, sizeof(gptpart_device_t));
        if (!device) {
            xprintf("gpt: out of memory!\n");
            txn->ops->release(txn);
            goto unbind;
        }

        char name[128];
        snprintf(name, sizeof(name), "%sp%u", dev->name, partitions);
        device_init(&device->device, drv, name, &gpt_proto);

        device->blksize = blksize;
        txn->ops->copyfrom(txn, &device->gpt_entry, sizeof(gpt_entry_t), sizeof(gpt_entry_t) * partitions);
        if (device->gpt_entry.type[0] == 0) {
            free(device);
            continue;
        }

        char guid[40];
        uint8_to_guid_string(guid, device->gpt_entry.type);
        char pname[40];
        utf16_to_cstring(pname, device->gpt_entry.name, GPT_NAME_LEN);
        xprintf("gpt: partition %u (%s) type=%s name=%s\n", partitions, device->device.name, guid, pname);

        device->device.protocol_id = MX_PROTOCOL_BLOCK;
        if (device_add(&device->device, dev) != NO_ERROR) {
            printf("gpt device_add failed\n");
            free(device);
            continue;
        }
    }

    txn->ops->release(txn);

    return NO_ERROR;
unbind:
    if (partitions == 0) {
        driver_unbind(drv, dev);
    }
    return NO_ERROR;
}

static mx_status_t gpt_bind(mx_driver_t* drv, mx_device_t* dev) {
    gpt_bind_info_t* info = malloc(sizeof(gpt_bind_info_t));
    info->drv = drv;
    info->dev = dev;

    // read partition table asynchronously
    thrd_t t;
    mx_status_t status = thrd_create_with_name(&t, gpt_bind_thread, info, "gpt-init");
    if (status < 0) {
        free(info);
        return status;
    }
    return NO_ERROR;
}

mx_driver_t _driver_gpt= {
    .ops = {
        .bind = gpt_bind,
    },
    .flags = DRV_FLAG_NO_AUTOBIND,
};

MAGENTA_DRIVER_BEGIN(_driver_gpt, "gpt", "magenta", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK),
MAGENTA_DRIVER_END(_driver_gpt)
