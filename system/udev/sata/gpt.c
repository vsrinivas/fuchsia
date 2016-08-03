// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>

#include <magenta/syscalls-ddk.h>
#include <magenta/types.h>
#include <runtime/completion.h>
#include <runtime/mutex.h>
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

#define TXN_SIZE PAGE_SIZE // just a random choice

typedef struct gpt_part_device {
    mx_device_t device;
    gpt_entry_t gpt_entry;
    uint64_t blksize;
} gpt_partdev_t;

#define get_gpt_device(dev) containerof(dev, gpt_partdev_t, device)

static void uint8_to_guid_string(char* dst, uint8_t* src) {
    sprintf(dst, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", src[3], src[2], src[1], src[0], src[5], src[4], src[7], src[6], src[9], src[8], src[15], src[14], src[13], src[12], src[11], src[10]);
}

static void utf16_to_cstring(char* dst, uint8_t* src, size_t count) {
    while (count--) {
        *dst++ = *src;
        src += 2; // FIXME cheesy
    }
}

static size_t gpt_getsize(gpt_partdev_t* dev) {
    // last LBA is inclusive
    uint64_t lbacount = dev->gpt_entry.last_lba - dev->gpt_entry.first_lba + 1;
    return lbacount * dev->blksize;
}

static void gpt_read_sync_complete(iotxn_t* txn) {
    mxr_completion_signal((mxr_completion_t*)txn->context);
}

// implement device protocol:

static ssize_t gpt_partdev_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    gpt_partdev_t* device = get_gpt_device(dev);
    iotxn_t* txn;
    mx_status_t status = iotxn_alloc(&txn, 0, TXN_SIZE, 0);
    if (status != NO_ERROR) {
        xprintf("%s: error %d allocating iotxn\n", dev->name, status);
        return status;
    }

    mxr_completion_t completion = MXR_COMPLETION_INIT;

    // offset must be aligned to block size
    if (off % device->blksize) {
        xprintf("%s: offset 0x%llx is not aligned to blksize=%llu!\n", dev->name, off, device->blksize);
        return ERR_INVALID_ARGS;
    }

    // sanity check
    uint64_t off_lba = off / device->blksize;
    uint64_t first = device->gpt_entry.first_lba;
    uint64_t last = device->gpt_entry.last_lba;
    if (first + off_lba > last) {
        xprintf("%s: offset 0x%llx is past the end of partition!\n", dev->name, off);
        return ERR_INVALID_ARGS;
    }
    // constrain if too many bytes are requested
    uint64_t c = MIN((last - (first + off_lba)) * device->blksize, count);

    // queue iotxn's until c bytes is read
    mx_off_t offset = off;
    mx_off_t toff = 0;
    void* ptr = buf;
    while (c > 0) {
        txn->opcode = IOTXN_OP_READ;
        txn->offset = first * device->blksize + offset;
        txn->length = c > TXN_SIZE ? TXN_SIZE : c;
        txn->complete_cb = gpt_read_sync_complete;
        txn->context = &completion;

        iotxn_queue(dev->parent, txn);
        mxr_completion_wait(&completion, MX_TIME_INFINITE);

        if (txn->status != NO_ERROR) {
            xprintf("%s: error %d in iotxn\n", dev->name, txn->status);
            txn->ops->release(txn);
            return txn->status;
        }

        // get the data!
        txn->ops->copyfrom(txn, ptr, txn->actual, toff);

        ptr += txn->actual;
        offset += txn->actual;
        toff += txn->actual;
        c -= txn->actual;

        // give up if the device read fewer bytes than requested
        if (txn->actual < txn->length) break;

        // reset so we can keep reading
        mxr_completion_reset(&completion);
    }

    txn->ops->release(txn);
    return toff;
}

static ssize_t gpt_partdev_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    return ERR_NOT_SUPPORTED;
}

static ssize_t gpt_partdev_ioctl(mx_device_t* dev, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max) {
    gpt_partdev_t* device = get_gpt_device(dev);
    switch (op) {
    case BLOCK_OP_GET_SIZE: {
        uint64_t* size = reply;
        if (max < sizeof(*size)) return ERR_NOT_ENOUGH_BUFFER;
        *size = gpt_getsize(device);
        return sizeof(*size);
    }
    case BLOCK_OP_GET_BLOCKSIZE: {
        uint64_t* blksize = reply;
        if (max < sizeof(*blksize)) return ERR_NOT_ENOUGH_BUFFER;
        *blksize = device->blksize;
        return sizeof(*blksize);
    }
    case BLOCK_OP_GET_GUID: {
        char* guid = reply;
        if (max < GPT_GUID_STRLEN) return ERR_NOT_ENOUGH_BUFFER;
        uint8_to_guid_string(guid, device->gpt_entry.type);
        return GPT_GUID_STRLEN;
    }
    case BLOCK_OP_GET_NAME: {
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

static mx_off_t gpt_partdev_getsize(mx_device_t* dev) {
    return gpt_getsize(get_gpt_device(dev));
}

static mx_protocol_device_t gpt_partdev_proto = {
    .read = gpt_partdev_read,
    .write = gpt_partdev_write,
    .ioctl = gpt_partdev_ioctl,
    .get_size = gpt_partdev_getsize,
};

static mx_status_t gpt_bind(mx_driver_t* drv, mx_device_t* dev) {
    uint64_t blksize;
    ssize_t rc = dev->ops->ioctl(dev, BLOCK_OP_GET_BLOCKSIZE, NULL, 0, &blksize, sizeof(blksize));
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

    mxr_completion_t completion = MXR_COMPLETION_INIT;

    // read partition table header synchronously (LBA1)
    txn->opcode = IOTXN_OP_READ;
    txn->offset = blksize;
    txn->length = blksize;
    txn->complete_cb = gpt_read_sync_complete;
    txn->context = &completion;

    iotxn_queue(dev, txn);
    mxr_completion_wait(&completion, MX_TIME_INFINITE);

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
    txn->context = &completion;

    mxr_completion_reset(&completion);
    iotxn_queue(dev, txn);
    mxr_completion_wait(&completion, MX_TIME_INFINITE);

    for (unsigned i = 0; i < header.entries_count; i++) {
        if (i * header.entries_sz > txn->actual) break;

        gpt_partdev_t* device = calloc(1, sizeof(gpt_partdev_t));
        if (!device) {
            xprintf("gpt: out of memory!\n");
            txn->ops->release(txn);
            return ERR_NO_MEMORY;
        }

        char name[8];
        snprintf(name, sizeof(name), "part%u", i);
        status = device_init(&device->device, drv, name, &gpt_partdev_proto);
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

static mx_bind_inst_t binding[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK),
};

mx_driver_t _driver_gpt BUILTIN_DRIVER = {
    .name = "gpt",
    .ops = {
        .bind = gpt_bind,
    },
    .binding = binding,
    .binding_size = sizeof(binding),
};
