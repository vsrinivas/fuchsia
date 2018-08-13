// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/protocol/block.h>

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <lib/cksum.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <lib/sync/completion.h>
#include <threads.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <zircon/hw/gpt.h>

typedef gpt_header_t gpt_t;

#define TXN_SIZE 0x4000 // 128 partition entries

typedef struct gptpart_device {
    zx_device_t* zxdev;
    zx_device_t* parent;

    block_protocol_t bp;

    gpt_entry_t gpt_entry;

    block_info_t info;
    size_t block_op_size;
} gptpart_device_t;

struct guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

static void uint8_to_guid_string(char* dst, uint8_t* src) {
    struct guid* guid = (struct guid*)src;
    sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2,
            guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
            guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

static void utf16_to_cstring(char* dst, uint8_t* src, size_t charcount) {
    while (charcount > 0) {
        *dst++ = *src;
        src += 2; // FIXME cheesy
        charcount -= 2;
    }
}

static uint64_t get_lba_count(gptpart_device_t* dev) {
    // last LBA is inclusive
    return dev->gpt_entry.last - dev->gpt_entry.first + 1;
}

static bool validate_header(const gpt_t* header, const block_info_t* info) {
    if (header->size > sizeof(gpt_t)) {
        zxlogf(ERROR, "gpt: invalid header size\n");
        return false;
    }
    if (header->magic != GPT_MAGIC) {
        zxlogf(ERROR, "gpt: bad header magic\n");
        return false;
    }
    gpt_t copy;
    memcpy(&copy, header, sizeof(gpt_t));
    copy.crc32 = 0;
    uint32_t crc = crc32(0, (const unsigned char*)&copy, copy.size);
    if (crc != header->crc32) {
        zxlogf(ERROR, "gpt: header crc invalid\n");
        return false;
    }
    if (header->last >= info->block_count) {
        zxlogf(ERROR, "gpt: last block > block count\n");
        return false;
    }
    if (header->entries_count * header->entries_size > TXN_SIZE) {
        zxlogf(ERROR, "gpt: entry table too big\n");
        return false;
    }
    return true;
}

// implement device protocol:

static zx_status_t gpt_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmdlen,
                         void* reply, size_t max, size_t* out_actual) {
    gptpart_device_t* device = ctx;
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reply;
        if (max < sizeof(*info))
            return ZX_ERR_BUFFER_TOO_SMALL;
        memcpy(info, &device->info, sizeof(*info));
        *out_actual = sizeof(*info);
        return ZX_OK;
    }
    case IOCTL_BLOCK_GET_TYPE_GUID: {
        char* guid = reply;
        if (max < GPT_GUID_LEN) return ZX_ERR_BUFFER_TOO_SMALL;
        memcpy(guid, device->gpt_entry.type, GPT_GUID_LEN);
        *out_actual = GPT_GUID_LEN;
        return ZX_OK;
    }
    case IOCTL_BLOCK_GET_PARTITION_GUID: {
        char* guid = reply;
        if (max < GPT_GUID_LEN) return ZX_ERR_BUFFER_TOO_SMALL;
        memcpy(guid, device->gpt_entry.guid, GPT_GUID_LEN);
        *out_actual = GPT_GUID_LEN;
        return ZX_OK;
    }
    case IOCTL_BLOCK_GET_NAME: {
        char* name = reply;
        memset(name, 0, max);
        // save room for the null terminator
        utf16_to_cstring(name, device->gpt_entry.name, MIN((max - 1) * 2, GPT_NAME_LEN));
        *out_actual = strnlen(name, GPT_NAME_LEN / 2);
        return ZX_OK;
    }
    case IOCTL_DEVICE_SYNC: {
        // Propagate sync to parent device
        return device_ioctl(device->parent, IOCTL_DEVICE_SYNC, NULL, 0, NULL, 0, NULL);
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void gpt_query(void* ctx, block_info_t* bi, size_t* bopsz) {
    gptpart_device_t* gpt = ctx;
    memcpy(bi, &gpt->info, sizeof(block_info_t));
    *bopsz = gpt->block_op_size;
}

static void gpt_queue(void* ctx, block_op_t* bop) {
    gptpart_device_t* gpt = ctx;

    switch (bop->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
        size_t blocks = bop->rw.length;
        size_t max = get_lba_count(gpt);

        // Ensure that the request is in-bounds
        if ((bop->rw.offset_dev >= max) ||
            ((max - bop->rw.offset_dev) < blocks)) {
            bop->completion_cb(bop, ZX_ERR_OUT_OF_RANGE);
            return;
        }

        // Adjust for partition starting block
        bop->rw.offset_dev += gpt->gpt_entry.first;
        break;
    }
    case BLOCK_OP_FLUSH:
        break;
    default:
        bop->completion_cb(bop, ZX_ERR_NOT_SUPPORTED);
        return;
    }

    gpt->bp.ops->queue(gpt->bp.ctx, bop);
}

static void gpt_unbind(void* ctx) {
    gptpart_device_t* device = ctx;
    device_remove(device->zxdev);
}

static void gpt_release(void* ctx) {
    gptpart_device_t* device = ctx;
    free(device);
}

static zx_off_t gpt_get_size(void* ctx) {
    gptpart_device_t* dev = ctx;
    //TODO: use query() results, *but* fvm returns different query and getsize
    // results, and the latter are dynamic...
    return device_get_size(dev->parent);
}

static zx_protocol_device_t gpt_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = gpt_ioctl,
    .get_size = gpt_get_size,
    .unbind = gpt_unbind,
    .release = gpt_release,
};

static block_protocol_ops_t block_ops = {
    .query = gpt_query,
    .queue = gpt_queue,
};

static void gpt_read_sync_complete(block_op_t* bop, zx_status_t status) {
    // Pass 32bit status back to caller via 32bit command field
    // Saves from needing custom structs, etc.
    bop->command = status;
    sync_completion_signal((sync_completion_t*)bop->cookie);
}

static zx_status_t vmo_read(zx_handle_t vmo, void* data, uint64_t off, size_t len) {
    return zx_vmo_read(vmo, data, off, len);
}

static int gpt_bind_thread(void* arg) {
    gptpart_device_t* first_dev = (gptpart_device_t*)arg;
    zx_device_t* dev = first_dev->parent;

    // used to keep track of number of partitions found
    unsigned partitions = 0;

    block_protocol_t bp;
    memcpy(&bp, &first_dev->bp, sizeof(bp));

    block_info_t block_info;
    size_t block_op_size;
    bp.ops->query(bp.ctx, &block_info, &block_op_size);

    zx_handle_t vmo = ZX_HANDLE_INVALID;
    block_op_t* bop = calloc(1, block_op_size);
    if (bop == NULL) {
        goto unbind;
    }

    if (zx_vmo_create(TXN_SIZE, 0, &vmo) != ZX_OK) {
        zxlogf(ERROR, "gpt: cannot allocate vmo\n");
        goto unbind;
    }

    // sanity check the default txn size with the block size
    if ((TXN_SIZE % block_info.block_size) || (TXN_SIZE < block_info.block_size)) {
        zxlogf(ERROR, "gpt: default txn size=%d is not aligned to blksize=%u!\n",
               TXN_SIZE, block_info.block_size);
        goto unbind;
    }

    sync_completion_t completion = SYNC_COMPLETION_INIT;

    // read partition table header synchronously (LBA1)
    bop->command = BLOCK_OP_READ;
    bop->rw.vmo = vmo;
    bop->rw.length = 1;
    bop->rw.offset_dev = 1;
    bop->rw.offset_vmo = 0;
    bop->rw.pages = NULL;
    bop->completion_cb = gpt_read_sync_complete;
    bop->cookie = &completion;

    bp.ops->queue(bp.ctx, bop);
    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    if (bop->command != ZX_OK) {
        zxlogf(ERROR, "gpt: error %d reading partition header\n", bop->command);
        goto unbind;
    }

    // read the header
    gpt_t header;
    if (vmo_read(vmo, &header, 0, sizeof(gpt_t)) != ZX_OK) {
        goto unbind;
    }
    if (!validate_header(&header, &block_info)) {
        goto unbind;
    }

    zxlogf(SPEW, "gpt: found gpt header %u entries @ lba%" PRIu64 "\n",
           header.entries_count, header.entries);

    // read partition table entries
    size_t table_sz = header.entries_count * header.entries_size;
    if (table_sz > TXN_SIZE) {
        zxlogf(INFO, "gpt: partition table is larger than the buffer!\n");
        // FIXME read the whole partition table. ok for now because on pixel2, this is
        // enough to read the entries that actually contain valid data
        table_sz = TXN_SIZE;
    }

    bop->command = BLOCK_OP_READ;
    bop->rw.vmo = vmo;
    bop->rw.length = (table_sz + (block_info.block_size - 1)) / block_info.block_size;
    bop->rw.offset_dev = header.entries;
    bop->rw.offset_vmo = 0;
    bop->rw.pages = NULL;

    sync_completion_reset(&completion);
    bp.ops->queue(bp.ctx, bop);
    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    if (bop->command != ZX_OK) {
        zxlogf(ERROR, "gpt: error %d reading partition table\n", bop->command);
        goto unbind;
    }

    uint8_t entries[TXN_SIZE];
    if (vmo_read(vmo, entries, 0, TXN_SIZE) != ZX_OK) {
        goto unbind;
    }

    uint32_t crc = crc32(0, (const unsigned char*)entries, table_sz);
    if (crc != header.entries_crc) {
        zxlogf(ERROR, "gpt: entries crc invalid\n");
        goto unbind;
    }

    uint64_t dev_block_count = block_info.block_count;

    for (partitions = 0; partitions < header.entries_count; partitions++) {
        if (partitions * header.entries_size > table_sz) break;

        // skip over entries that look invalid
        gpt_entry_t* entry = (gpt_entry_t*)(entries + (partitions * sizeof(gpt_entry_t)));
        if (entry->first < header.first || entry->last > header.last) {
            continue;
        }
        if (entry->first == entry->last) {
            continue;
        }
        if ((entry->last - entry->first + 1) > dev_block_count) {
            zxlogf(ERROR, "gpt: entry %u too large, last = 0x%" PRIx64
                   " first = 0x%" PRIx64 " block_count = 0x%" PRIx64 "\n",
                   partitions, entry->last, entry->first, dev_block_count);
            continue;
        }

        gptpart_device_t* device;
        // use first_dev for first partition
        if (first_dev) {
            device = first_dev;
        } else {
            device = calloc(1, sizeof(gptpart_device_t));
            if (!device) {
                zxlogf(ERROR, "gpt: out of memory!\n");
                goto unbind;
            }
            device->parent = dev;
            memcpy(&device->bp, &bp, sizeof(bp));
        }

        memcpy(&device->gpt_entry, entry, sizeof(gpt_entry_t));
        block_info.block_count = device->gpt_entry.last - device->gpt_entry.first + 1;
        memcpy(&device->info, &block_info, sizeof(block_info));
        device->block_op_size = block_op_size;

        char type_guid[GPT_GUID_STRLEN];
        uint8_to_guid_string(type_guid, device->gpt_entry.type);
        char partition_guid[GPT_GUID_STRLEN];
        uint8_to_guid_string(partition_guid, device->gpt_entry.guid);
        char pname[GPT_NAME_LEN];
        utf16_to_cstring(pname, device->gpt_entry.name, GPT_NAME_LEN);

        if (first_dev) {
            // make our initial device visible and use if for partition zero
            device_make_visible(first_dev->zxdev);
            first_dev = NULL;
        } else {
            char name[128];
            snprintf(name, sizeof(name), "part-%03u", partitions);

            zxlogf(SPEW, "gpt: partition %u (%s) type=%s guid=%s name=%s first=0x%"
                   PRIx64 " last=0x%" PRIx64 "\n",
                   partitions, name, type_guid, partition_guid, pname,
                   device->gpt_entry.first, device->gpt_entry.last);

            device_add_args_t args = {
                .version = DEVICE_ADD_ARGS_VERSION,
                .name = name,
                .ctx = device,
                .ops = &gpt_proto,
                .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
                .proto_ops = &block_ops,
            };

            if (device_add(dev, &args, &device->zxdev) != ZX_OK) {
                free(device);
                continue;
            }
        }
    }

    free(bop);
    zx_handle_close(vmo);
    return 0;

unbind:
    free(bop);
    zx_handle_close(vmo);
    if (first_dev) {
        // handle case where no partitions were found
        device_remove(first_dev->zxdev);
    }
    return -1;
}

static zx_status_t gpt_bind(void* ctx, zx_device_t* parent) {
    // create an invisible device, which will be used for the first partition
    gptpart_device_t* device = calloc(1, sizeof(gptpart_device_t));
    if (!device) {
        return ZX_ERR_NO_MEMORY;
    }
    device->parent = parent;

    if (device_get_protocol(parent, ZX_PROTOCOL_BLOCK, &device->bp) != ZX_OK) {
        zxlogf(ERROR, "gpt: ERROR: block device '%s': does not support block protocol\n",
               device_get_name(parent));
        free(device);
        return ZX_ERR_NOT_SUPPORTED;
    }

    char name[128];
    snprintf(name, sizeof(name), "part-%03u", 0);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = name,
        .ctx = device,
        .ops = &gpt_proto,
        .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
        .proto_ops = &block_ops,
        .flags = DEVICE_ADD_INVISIBLE,
    };

    zx_status_t status = device_add(parent, &args, &device->zxdev);
    if (status != ZX_OK) {
        free(device);
        return status;
    }

    // read partition table asynchronously
    thrd_t t;
    status = thrd_create_with_name(&t, gpt_bind_thread, device, "gpt-init");
    if (status != ZX_OK) {
        device_remove(device->zxdev);
    }
    return status;
}

static zx_driver_ops_t gpt_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = gpt_bind,
};

ZIRCON_DRIVER_BEGIN(gpt, gpt_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK),
ZIRCON_DRIVER_END(gpt)
