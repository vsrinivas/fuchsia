// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <magenta/listnode.h>
#include <sys/param.h>
#include <sync/completion.h>
#include <lib/cksum.h>
#include <assert.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <threads.h>

#include "gpt.h"

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

#define TXN_SIZE 0x4000 // 128 partition entries

typedef struct gptpart_device {
    mx_device_t* mxdev;
    mx_device_t* parent;

    gpt_entry_t gpt_entry;

    block_info_t info;
    block_callbacks_t* callbacks;

    atomic_int writercount;
} gptpart_device_t;

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
    return lbacount * dev->info.block_size;
}

static mx_off_t to_parent_offset(gptpart_device_t* dev, mx_off_t offset) {
    return offset + dev->gpt_entry.first_lba * dev->info.block_size;
}

static bool validate_header(const gpt_t* header, const block_info_t* info) {
    if (header->size > sizeof(gpt_t)) {
        xprintf("gpt: invalid header size\n");
        return false;
    }
    gpt_t copy;
    memcpy(&copy, header, sizeof(gpt_t));
    copy.crc32 = 0;
    uint32_t crc = crc32(0, (const unsigned char*)&copy, copy.size);
    if (crc != header->crc32) {
        xprintf("gpt: header crc invalid\n");
        return false;
    }
    if (header->last_lba >= info->block_count) {
        xprintf("gpt: last block > block count\n");
        return false;
    }
    if (header->entries_count * header->entries_sz > TXN_SIZE) {
        xprintf("gpt: entry table too big\n");
        return false;
    }
    return true;
}

// implement device protocol:

static mx_status_t gpt_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen,
                         void* reply, size_t max, size_t* out_actual) {
    gptpart_device_t* device = ctx;
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reply;
        if (max < sizeof(*info))
            return MX_ERR_BUFFER_TOO_SMALL;
        memcpy(info, &device->info, sizeof(*info));
        *out_actual = sizeof(*info);
        return MX_OK;
    }
    case IOCTL_BLOCK_GET_TYPE_GUID: {
        char* guid = reply;
        if (max < GPT_GUID_LEN) return MX_ERR_BUFFER_TOO_SMALL;
        memcpy(guid, device->gpt_entry.type, GPT_GUID_LEN);
        return GPT_GUID_LEN;
        *out_actual = GPT_GUID_LEN;
        return MX_OK;
    }
    case IOCTL_BLOCK_GET_PARTITION_GUID: {
        char* guid = reply;
        if (max < GPT_GUID_LEN) return MX_ERR_BUFFER_TOO_SMALL;
        memcpy(guid, device->gpt_entry.guid, GPT_GUID_LEN);
        *out_actual = GPT_GUID_LEN;
        return MX_OK;
    }
    case IOCTL_BLOCK_GET_NAME: {
        char* name = reply;
        memset(name, 0, max);
        // save room for the null terminator
        utf16_to_cstring(name, device->gpt_entry.name, MIN((max - 1) * 2, GPT_NAME_LEN));
        *out_actual = strnlen(name, GPT_NAME_LEN / 2);
        return MX_OK;
    }
    case IOCTL_DEVICE_SYNC: {
        // Propagate sync to parent device
        return device_ioctl(device->parent, IOCTL_DEVICE_SYNC, NULL, 0, NULL, 0, NULL);
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

static void gpt_iotxn_queue(void* ctx, iotxn_t* txn) {
    gptpart_device_t* device = ctx;
    if (txn->offset % device->info.block_size) {
        iotxn_complete(txn, MX_ERR_INVALID_ARGS, 0);
        return;
    }
    if (txn->offset > getsize(device)) {
        iotxn_complete(txn, MX_ERR_OUT_OF_RANGE, 0);
        return;
    }
    // transactions from read()/write() may be truncated
    txn->length = ROUNDDOWN(txn->length, device->info.block_size);
    txn->length = MIN(txn->length, getsize(device) - txn->offset);
    txn->offset = to_parent_offset(device, txn->offset);
    if (txn->length == 0) {
        iotxn_complete(txn, MX_OK, 0);
    } else {
        iotxn_queue(device->parent, txn);
    }
}

static mx_off_t gpt_getsize(void* ctx) {
    gptpart_device_t* device = ctx;
    return getsize(device);
}

static void gpt_unbind(void* ctx) {
    gptpart_device_t* device = ctx;
    device_remove(device->mxdev);
}

static void gpt_release(void* ctx) {
    gptpart_device_t* device = ctx;
    free(device);
}

static inline bool is_writer(uint32_t flags) {
    return (flags & O_RDWR || flags & O_WRONLY);
}

static mx_status_t gpt_open(void* ctx, mx_device_t** dev_out, uint32_t flags) {
    gptpart_device_t* device = ctx;
    mx_status_t status = MX_OK;
    if (is_writer(flags) && (atomic_exchange(&device->writercount, 1) == 1)) {
        printf("Partition cannot be opened as writable (open elsewhere)\n");
        status = MX_ERR_ALREADY_BOUND;
    }
    return status;
}

static mx_status_t gpt_close(void* ctx, uint32_t flags) {
    gptpart_device_t* device = ctx;
    if (is_writer(flags)) {
        atomic_fetch_sub(&device->writercount, 1);
    }
    return MX_OK;
}

static mx_protocol_device_t gpt_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = gpt_ioctl,
    .iotxn_queue = gpt_iotxn_queue,
    .get_size = gpt_getsize,
    .unbind = gpt_unbind,
    .release = gpt_release,
    .open = gpt_open,
    .close = gpt_close,
};

static void gpt_block_set_callbacks(void* ctx, block_callbacks_t* cb) {
    gptpart_device_t* device = ctx;
    device->callbacks = cb;
}

static void gpt_block_get_info(void* ctx, block_info_t* info) {
    gptpart_device_t* device = ctx;
    memcpy(info, &device->info, sizeof(*info));
}

static void gpt_block_complete(iotxn_t* txn, void* cookie) {
    gptpart_device_t* dev;
    memcpy(&dev, txn->extra, sizeof(gptpart_device_t*));
    dev->callbacks->complete(cookie, txn->status);
    iotxn_release(txn);
}

static void block_do_txn(gptpart_device_t* dev, uint32_t opcode, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    block_info_t* info = &dev->info;
    if ((dev_offset % info->block_size) || (length % info->block_size)) {
        dev->callbacks->complete(cookie, MX_ERR_INVALID_ARGS);
        return;
    }
    uint64_t size = getsize(dev);
    if ((dev_offset >= size) || (length >= (size - dev_offset))) {
        dev->callbacks->complete(cookie, MX_ERR_OUT_OF_RANGE);
        return;
    }

    mx_status_t status;
    iotxn_t* txn;
    if ((status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, vmo, vmo_offset, length)) != MX_OK) {
        dev->callbacks->complete(cookie, status);
        return;
    }
    txn->opcode = opcode;
    txn->length = length;
    txn->offset = to_parent_offset(dev, dev_offset);
    txn->complete_cb = gpt_block_complete;
    txn->cookie = cookie;
    memcpy(txn->extra, &dev, sizeof(gptpart_device_t*));
    iotxn_queue(dev->parent, txn);
}

static void gpt_block_read(void* ctx, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    block_do_txn(ctx, IOTXN_OP_READ, vmo, length, vmo_offset, dev_offset, cookie);
}

static void gpt_block_write(void* ctx, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    block_do_txn(ctx, IOTXN_OP_WRITE, vmo, length, vmo_offset, dev_offset, cookie);
}

static block_protocol_ops_t gpt_block_ops = {
    .set_callbacks = gpt_block_set_callbacks,
    .get_info = gpt_block_get_info,
    .read = gpt_block_read,
    .write = gpt_block_write,
};

static void gpt_read_sync_complete(iotxn_t* txn, void* cookie) {
    completion_signal((completion_t*)cookie);
}

typedef struct gpt_bind_info {
    mx_device_t* dev;
} gpt_bind_info_t;

static int gpt_bind_thread(void* arg) {
    gpt_bind_info_t* info = (gpt_bind_info_t*)arg;
    mx_device_t* dev = info->dev;
    free(info);

    iotxn_t* txn = NULL;
    unsigned partitions = 0; // used to keep track of number of partitions found
    block_info_t block_info;
    size_t actual = 0;
    ssize_t rc = device_ioctl(dev, IOCTL_BLOCK_GET_INFO, NULL, 0,
                              &block_info, sizeof(block_info), &actual);
    if (rc < 0 || actual != sizeof(block_info)) {
        xprintf("gpt: Error %zd getting blksize for dev=%s\n", rc, device_get_name(dev));
        goto unbind;
    }

    // sanity check the default txn size with the block size
    if (TXN_SIZE % block_info.block_size) {
        xprintf("gpt: default txn size=%d is not aligned to blksize=%u!\n", TXN_SIZE,
                block_info.block_size);
    }

    // allocate an iotxn to read the partition table
    mx_status_t status = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS, TXN_SIZE);
    if (status != MX_OK) {
        xprintf("gpt: error %d allocating iotxn\n", status);
        goto unbind;
    }

    completion_t completion = COMPLETION_INIT;

    // read partition table header synchronously (LBA1)
    txn->opcode = IOTXN_OP_READ;
    txn->offset = block_info.block_size;
    txn->length = block_info.block_size;
    txn->complete_cb = gpt_read_sync_complete;
    txn->cookie = &completion;

    iotxn_queue(dev, txn);
    completion_wait(&completion, MX_TIME_INFINITE);

    if (txn->status != MX_OK) {
        xprintf("gpt: error %d reading partition header\n", txn->status);
        goto unbind;
    }

    // read the header
    gpt_t header;
    iotxn_copyfrom(txn, &header, sizeof(gpt_t), 0);
    if (header.magic != GPT_MAGIC) {
        xprintf("gpt: bad header magic\n");
        goto unbind;
    }

    if (!validate_header(&header, &block_info)) {
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
    txn->offset = header.entries * block_info.block_size;
    txn->length = table_sz;
    txn->complete_cb = gpt_read_sync_complete;
    txn->cookie = &completion;

    completion_reset(&completion);
    iotxn_queue(dev, txn);
    completion_wait(&completion, MX_TIME_INFINITE);

    uint8_t entries[TXN_SIZE];
    iotxn_copyfrom(txn, entries, txn->actual, 0);

    uint32_t crc = crc32(0, (const unsigned char*)entries, txn->actual);
    if (crc != header.entries_crc) {
        xprintf("gpt: entries crc invalid\n");
        goto unbind;
    }

    uint64_t dev_block_count = block_info.block_count;

    for (partitions = 0; partitions < header.entries_count; partitions++) {
        if (partitions * header.entries_sz > txn->actual) break;

        // skip over entries that look invalid
        gpt_entry_t* entry = (gpt_entry_t*)(entries + (partitions * sizeof(gpt_entry_t)));
        if (entry->first_lba < header.first_lba || entry->last_lba > header.last_lba) {
            continue;
        }
        if (entry->first_lba == entry->last_lba) {
            continue;
        }
        if ((entry->last_lba - entry->first_lba + 1) > dev_block_count) {
            xprintf("gpt: entry %u too big, last = 0x%" PRIx64 " first = 0x%" PRIx64 " block_count = 0x%" PRIx64 "\n", partitions, entry->last_lba, entry->first_lba, dev_block_count);
            continue;
        }

        gptpart_device_t* device = calloc(1, sizeof(gptpart_device_t));
        if (!device) {
            xprintf("gpt: out of memory!\n");
            goto unbind;
        }
        device->parent = dev;

        memcpy(&device->gpt_entry, entry, sizeof(gpt_entry_t));
        block_info.block_count = device->gpt_entry.last_lba - device->gpt_entry.first_lba + 1;
        memcpy(&device->info, &block_info, sizeof(block_info));

        char type_guid[GPT_GUID_STRLEN];
        uint8_to_guid_string(type_guid, device->gpt_entry.type);
        char partition_guid[GPT_GUID_STRLEN];
        uint8_to_guid_string(partition_guid, device->gpt_entry.guid);
        char pname[GPT_NAME_LEN];
        utf16_to_cstring(pname, device->gpt_entry.name, GPT_NAME_LEN);

        char name[128];
        snprintf(name, sizeof(name), "part-%03u", partitions);

        xprintf("gpt: partition %u (%s) type=%s guid=%s name=%s first=0x%" PRIx64 " last=0x%" PRIx64 "\n",
                partitions, name, type_guid, partition_guid, pname,
                device->gpt_entry.first_lba, device->gpt_entry.last_lba);

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = name,
            .ctx = device,
            .ops = &gpt_proto,
            .proto_id = MX_PROTOCOL_BLOCK_CORE,
            .proto_ops = &gpt_block_ops,
        };

        if (device_add(dev, &args, &device->mxdev) != MX_OK) {
            printf("gpt device_add failed\n");
            free(device);
            continue;
        }
    }

    iotxn_release(txn);

    return MX_OK;
unbind:
    if (txn != NULL) {
        iotxn_release(txn);
    }
    if (partitions == 0) {
        device_unbind(dev);
    }
    return MX_OK;
}

static mx_status_t gpt_bind(void* ctx, mx_device_t* dev, void** cookie) {
    gpt_bind_info_t* info = malloc(sizeof(gpt_bind_info_t));
    info->dev = dev;

    // read partition table asynchronously
    thrd_t t;
    mx_status_t status = thrd_create_with_name(&t, gpt_bind_thread, info, "gpt-init");
    if (status < 0) {
        free(info);
        return status;
    }
    return MX_OK;
}

static mx_driver_ops_t gpt_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = gpt_bind,
};

MAGENTA_DRIVER_BEGIN(gpt, gpt_driver_ops, "magenta", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK),
MAGENTA_DRIVER_END(gpt)
