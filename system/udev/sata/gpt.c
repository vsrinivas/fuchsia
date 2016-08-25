// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/completion.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>

#include <magenta/syscalls-ddk.h>
#include <magenta/types.h>
#include <system/listnode.h>
#include <sys/param.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "gpt.h"
#include "sata.h"

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

static void utf16_to_cstring(char* dst, uint8_t* src, size_t count) {
    while (count--) {
        *dst++ = *src;
        src += 2; // FIXME cheesy
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
        if (max < sizeof(*size)) return ERR_NOT_ENOUGH_BUFFER;
        *size = getsize(device);
        return sizeof(*size);
    }
    case IOCTL_BLOCK_GET_BLOCKSIZE: {
        uint64_t* blksize = reply;
        if (max < sizeof(*blksize)) return ERR_NOT_ENOUGH_BUFFER;
        *blksize = device->blksize;
        return sizeof(*blksize);
    }
    case IOCTL_BLOCK_GET_GUID: {
        char* guid = reply;
        if (max < GPT_GUID_STRLEN) return ERR_NOT_ENOUGH_BUFFER;
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
        xprintf("%s: offset 0x%llx is past the end of partition!\n", dev->name, txn->offset);
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

static mx_status_t gpt_release(mx_device_t* dev) {
    gptpart_device_t* device = get_gptpart_device(dev);
    free(device);
    return NO_ERROR;
}

static mx_protocol_device_t gpt_proto = {
    .ioctl = gpt_ioctl,
    .iotxn_queue = gpt_iotxn_queue,
    .get_size = gpt_getsize,
    .release = gpt_release,
};

static void gpt_read_sync_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

static mx_status_t gpt_bind(mx_driver_t* drv, mx_device_t* dev) {
    uint64_t blksize;
    ssize_t rc = dev->ops->ioctl(dev, IOCTL_BLOCK_GET_BLOCKSIZE, NULL, 0, &blksize, sizeof(blksize));
    if (rc < 0) {
        xprintf("gpt: Error %zd getting blksize for dev=%s\n", rc, dev->name);
        return rc;
    }

    // sanity check the default txn size with the block size
    if (TXN_SIZE % blksize) {
        xprintf("gpt: default txn size=%d is not aligned to blksize=%llu!\n", TXN_SIZE, blksize);
    }

    // allocate an iotxn to read the partition table
    iotxn_t* txn;
    mx_status_t status = iotxn_alloc(&txn, 0, TXN_SIZE, 0);
    if (status != NO_ERROR) {
        xprintf("gpt: error %d allocating iotxn\n", status);
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
        return txn->status;
    }

    // read the header
    gpt_t header;
    txn->ops->copyfrom(txn, &header, sizeof(gpt_t), 0);
    if (header.magic != GPT_MAGIC) {
        xprintf("gpt: bad header magic\n");
        txn->ops->release(txn);
        return ERR_NOT_SUPPORTED;
    }

    xprintf("gpt: found gpt header %u entries @ lba%llu\n", header.entries_count, header.entries);

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

    for (unsigned i = 0; i < header.entries_count; i++) {
        if (i * header.entries_sz > txn->actual) break;

        gptpart_device_t* device = calloc(1, sizeof(gptpart_device_t));
        if (!device) {
            xprintf("gpt: out of memory!\n");
            txn->ops->release(txn);
            return ERR_NO_MEMORY;
        }

        char name[8];
        snprintf(name, sizeof(name), "part%u", i);
        status = device_init(&device->device, drv, name, &gpt_proto);
        if (status) {
            xprintf("gpt: failed to init device\n");
            txn->ops->release(txn);
            free(device);
            return status;
        }

        device->blksize = blksize;
        txn->ops->copyfrom(txn, &device->gpt_entry, sizeof(gpt_entry_t), sizeof(gpt_entry_t) * i);
        if (device->gpt_entry.type[0] == 0) {
            free(device);
            break;
        }

        device->device.protocol_id = MX_PROTOCOL_BLOCK;
        device_add(&device->device, dev);

        char guid[40];
        uint8_to_guid_string(guid, device->gpt_entry.type);
        char pname[40];
        utf16_to_cstring(pname, device->gpt_entry.name, GPT_NAME_LEN);
        xprintf("gpt: partition %u (%s) type=%s name=%s\n", i, device->device.name, guid, pname);
    }

    txn->ops->release(txn);

    return NO_ERROR;
}

static mx_status_t gpt_unbind(mx_driver_t* driver, mx_device_t* device) {
    mx_device_t* dev = NULL;
    mx_device_t* temp = NULL;
    list_for_every_entry_safe(&device->children, dev, temp, mx_device_t, node) {
        device_remove(dev);
    }
    return NO_ERROR;
}

static mx_bind_inst_t binding[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK),
};

mx_driver_t _driver_gpt BUILTIN_DRIVER = {
    .name = "gpt",
    .ops = {
        .bind = gpt_bind,
        .unbind = gpt_unbind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
