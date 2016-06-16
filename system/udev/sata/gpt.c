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

// TODO cheesy
typedef struct dma_mem {
    void* mem;
    mx_paddr_t mem_phys;
    size_t mem_sz;
    bool busy;
    mxr_mutex_t mutex;
} dma_mem_t;

static dma_mem_t dma;

static void uint8_to_guid_string(char* dst, uint8_t* src) {
    sprintf(dst, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", src[3], src[2], src[1], src[0], src[5], src[4], src[7], src[6], src[9], src[8], src[15], src[14], src[13], src[12], src[11], src[10]);
}

static void utf16_to_cstring(char* dst, uint8_t* src, size_t count) {
    while (count--) {
        *dst++ = *src;
        src += 2; // FIXME cheesy
    }
}

static ssize_t gpt_partdev_read(mx_device_t* dev, void* buf, size_t count, size_t off, void* cookie) {
    // read count bytes from LBA (off)
    xprintf("gpt_partde_read buf=%p count=%zu off=%zu\n", buf, count, off);
    // FIXME this is probably wrong
    gpt_partdev_t* device = get_gpt_device(dev);

    uint64_t off_lba = off / device->disk->sector_sz;
    uint64_t first = device->gpt_entry.first_lba;
    uint64_t last = device->gpt_entry.last_lba;
    if (first + off_lba > last) {
        xprintf("%s: offset %zu is past the end of partition!\n", dev->name, off);
        return ERR_INVALID_ARGS;
    }
    uint64_t c = MIN((last - (first + off_lba)) * device->disk->sector_sz, count);

    // FIXME should be able to do this
    if (c > dma.mem_sz) {
        xprintf("%s: try to read %zu bytes, max is %zu bytes\n", dev->name, count, dma.mem_sz);
        c = dma.mem_sz;
    }

    if (dma.busy) return ERR_BUSY;
    mxr_mutex_lock(&dma.mutex);
    dma.busy = true;
    mxr_mutex_unlock(&dma.mutex);

    sata_read_block_sync(device->disk, first + off_lba, dma.mem_phys, c);

    memcpy(buf, dma.mem, c);

    mxr_mutex_lock(&dma.mutex);
    dma.busy = false;
    mxr_mutex_unlock(&dma.mutex);
    return c;
}

static ssize_t gpt_partdev_write(mx_device_t* dev, const void* buf, size_t count, size_t off, void* cookie) {
    return ERR_NOT_SUPPORTED;
}

static ssize_t gpt_partdev_ioctl(mx_device_t* dev, uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max, void* cookie) {
    gpt_partdev_t* device = get_gpt_device(dev);
    switch (op) {
    case BLOCK_OP_GET_SIZE: {
        uint64_t* size = reply;
        if (max < sizeof(*size)) return ERR_NOT_ENOUGH_BUFFER;
        // last LBA is inclusive
        uint64_t lbacount = device->gpt_entry.last_lba - device->gpt_entry.first_lba + 1;
        *size = lbacount * device->disk->sector_sz;
        return sizeof(*size);
    }
    case BLOCK_OP_GET_BLOCKSIZE: {
        uint64_t* blksize = reply;
        if (max < sizeof(*blksize)) return ERR_NOT_ENOUGH_BUFFER;
        *blksize = device->disk->sector_sz;
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
        utf16_to_cstring(name, device->gpt_entry.name, MIN(max * 2, 72));
        return strnlen(name, 72 / 2);
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

// implement device protocol:

static mx_protocol_device_t gpt_partdev_proto = {
    .read = gpt_partdev_read,
    .write = gpt_partdev_write,
    .ioctl = gpt_partdev_ioctl,
};

mx_status_t gpt_bind(mx_driver_t* drv, mx_device_t* dev, sata_device_t* disk) {
    // allocate some dma memory
    void* data;
    mx_paddr_t data_phys;
    size_t data_sz = PAGE_SIZE * 4;
    mx_status_t status = mx_alloc_device_memory(data_sz, &data_phys, &data);
    if (status < 0) {
        xprintf("gpt: error allocating dma memory\n");
        return status;
    }

    // read partition table header (LBA1)
    status = sata_read_block_sync(disk, 1, data_phys, 512);
    if (status) {
        xprintf("gpt: error %d reading partition table header\n", status);
        return status;
    }

    gpt_t header;
    memcpy(&header, data, sizeof(gpt_t));
    if (header.magic != GPT_MAGIC) {
        xprintf("gpt: bad header magic\n");
    }

    xprintf("gpt: found gpt header %u entries @ lba%llu\n", header.entries_count, header.entries);

    // read partition table entries
    size_t table_sz = MIN(data_sz, header.entries_count * header.entries_sz);
    status = sata_read_block_sync(disk, header.entries, data_phys, table_sz);
    if (status) {
        xprintf("gpt: error %d reading partition table (LBA%llu)\n", status, header.entries);
    }

    gpt_entry_t* entry;
    for (unsigned i = 0; i < header.entries_count; i++) {
        if (i * header.entries_sz > data_sz) break;
        entry = data + (i * header.entries_sz);
        if (entry->type[0] == 0) break;

        gpt_partdev_t* device = calloc(1, sizeof(gpt_partdev_t));
        if (!device) {
            xprintf("gpt: out of memory!\n");
            return ERR_NO_MEMORY;
        }

        char name[8];
        snprintf(name, sizeof(name), "part%u", i);
        status = device_init(&device->device, drv, name, &gpt_partdev_proto);
        if (status) {
            xprintf("gpt: failed to init device\n");
            free(device);
            return status;
        }

        device->disk = disk;
        memcpy(&device->gpt_entry, entry, sizeof(gpt_entry_t));

        device->device.protocol_id = MX_PROTOCOL_BLOCK;
        device_add(&device->device, dev);

        char guid[40];
        uint8_to_guid_string(guid, device->gpt_entry.type);
        char pname[40];
        utf16_to_cstring(pname, device->gpt_entry.name, 72);
        xprintf("gpt: partition %u (%s) type=%s name=%s\n", i, device->device.name, guid, pname);
    }

    dma.mem = data;
    dma.mem_phys = data_phys;
    dma.mem_sz = data_sz;

    return NO_ERROR;
}
