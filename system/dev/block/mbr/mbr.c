// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>

#include <gpt/gpt.h>
#include <lib/sync/completion.h>
#include <zircon/device/block.h>
#include <zircon/threads.h>

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))
#define MBR_SIZE 512
#define MBR_PARTITION_ENTRY_SIZE 16
#define MBR_NUM_PARTITIONS 4
#define MBR_BOOT_SIGNATURE 0xAA55

// ATTN: MBR supports 8 bit partition types instead of GUIDs. Here we define
// mappings between partition type and GUIDs that zircon understands. When
// the MBR driver receives a request for the type GUID, we lie and return the
// a mapping from partition type to type GUID.
static const uint8_t data_guid[GPT_GUID_LEN] = GUID_DATA_VALUE;
static const uint8_t sys_guid[GPT_GUID_LEN] = GUID_SYSTEM_VALUE;
#define PARTITION_TYPE_NONE 0x00
#define PARTITION_TYPE_DATA 0xE9
#define PARTITION_TYPE_SYS 0xEA

typedef struct __PACKED mbr_partition_entry {
    uint8_t status;
    uint8_t chs_addr_start[3];
    uint8_t type;
    uint8_t chs_addr_end[3];
    uint32_t start_sector_lba;
    uint32_t sector_partition_length;
} mbr_partition_entry_t;

typedef struct __PACKED mbr {
    uint8_t bootstrap_code[446];
    mbr_partition_entry_t partition[MBR_NUM_PARTITIONS];
    uint16_t boot_signature;
} mbr_t;

typedef struct mbrpart_device {
    zx_device_t* zxdev;
    zx_device_t* parent;

    block_protocol_t bp;

    mbr_partition_entry_t partition;

    block_info_t info;
    size_t block_op_size;

    atomic_int writercount;
} mbrpart_device_t;

static zx_status_t mbr_ioctl(void* ctx, uint32_t op, const void* cmd,
                             size_t cmdlen, void* reply, size_t max,
                             size_t* out_actual) {
    mbrpart_device_t* device = ctx;
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
        if (max < GPT_GUID_LEN)
            return ZX_ERR_BUFFER_TOO_SMALL;
        if (device->partition.type == PARTITION_TYPE_DATA) {
            memcpy(guid, data_guid, GPT_GUID_LEN);
            *out_actual = GPT_GUID_LEN;
            return ZX_OK;
        } else if (device->partition.type == PARTITION_TYPE_SYS) {
            memcpy(guid, sys_guid, GPT_GUID_LEN);
            *out_actual = GPT_GUID_LEN;
            return ZX_OK;
        } else {
            return ZX_ERR_NOT_FOUND;
        }
    }
    case IOCTL_BLOCK_GET_PARTITION_GUID: {
        return ZX_ERR_NOT_SUPPORTED;
    }
    case IOCTL_BLOCK_GET_NAME: {
        char* name = reply;
        memset(name, 0, max);
        strncpy(name, device_get_name(device->zxdev), max);
        *out_actual = strnlen(name, max);
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

static zx_off_t to_parent_offset(mbrpart_device_t* dev, zx_off_t offset) {
    return offset + (uint64_t)(dev->partition.start_sector_lba) *
           (uint64_t)dev->info.block_size;
}


static void mbr_query(void* ctx, block_info_t* bi, size_t* bopsz) {
    mbrpart_device_t* mbr = ctx;
    memcpy(bi, &mbr->info, sizeof(block_info_t));
    *bopsz = mbr->block_op_size;
}

static void mbr_queue(void* ctx, block_op_t* bop) {
    mbrpart_device_t* mbr = ctx;

    switch (bop->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
        size_t blocks = bop->rw.length;
        size_t max = mbr->partition.sector_partition_length;

        // Ensure that the request is in-bounds
        if ((bop->rw.offset_dev >= max) ||
            ((max - bop->rw.offset_dev) < blocks)) {
            bop->completion_cb(bop, ZX_ERR_INVALID_ARGS);
            return;
        }

        // Adjust for partition starting block
        bop->rw.offset_dev += mbr->partition.start_sector_lba;
        break;
    }
    case BLOCK_OP_FLUSH:
        break;
    default:
        bop->completion_cb(bop, ZX_ERR_NOT_SUPPORTED);
        return;
    }

    mbr->bp.ops->queue(mbr->bp.ctx, bop);
}

static void mbr_unbind(void* ctx) {
    mbrpart_device_t* device = ctx;
    device_remove(device->zxdev);
}

static void mbr_release(void* ctx) {
    mbrpart_device_t* device = ctx;
    free(device);
}

static zx_off_t mbr_get_size(void* ctx) {
    mbrpart_device_t* dev = ctx;
    //TODO: use query() results, *but* fvm returns different query and getsize
    // results, and the latter are dynamic...
    return device_get_size(dev->parent);
}

static zx_protocol_device_t mbr_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = mbr_ioctl,
    .get_size = mbr_get_size,
    .unbind = mbr_unbind,
    .release = mbr_release,
};

static block_protocol_ops_t block_ops = {
    .query = mbr_query,
    .queue = mbr_queue,
};

static void mbr_read_sync_complete(block_op_t* bop, zx_status_t status) {
    bop->command = status;
    sync_completion_signal((sync_completion_t*)bop->cookie);
}

static zx_status_t vmo_read(zx_handle_t vmo, void* data, uint64_t off, size_t len) {
    return zx_vmo_read(vmo, data, off, len);
}

static int mbr_bind_thread(void* arg) {
    mbrpart_device_t* first_dev = (mbrpart_device_t*)arg;
    zx_device_t* dev = first_dev->parent;

    // Classic MBR supports 4 partitions.
    uint8_t partition_count = 0;

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

    // We need to read at least 512B to parse the MBR. Determine if we should
    // read the device's block size or we should ready exactly 512B.
    size_t iosize = 0;
    if (block_info.block_size >= MBR_SIZE) {
        iosize = block_info.block_size;
    } else {
        // Make sure we're reading some multiple of the block size.
        iosize = DIV_ROUND_UP(MBR_SIZE, block_info.block_size) * block_info.block_size;
    }

    if (zx_vmo_create(iosize, 0, &vmo) != ZX_OK) {
        zxlogf(ERROR, "mbr: cannot allocate vmo\n");
        goto unbind;
    }


    sync_completion_t cplt = SYNC_COMPLETION_INIT;

    bop->command = BLOCK_OP_READ;
    bop->rw.vmo = vmo;
    bop->rw.length = iosize / block_info.block_size;
    bop->rw.offset_dev = 0;
    bop->rw.offset_vmo = 0;
    bop->rw.pages = NULL;
    bop->completion_cb = mbr_read_sync_complete;
    bop->cookie = &cplt;

    bp.ops->queue(bp.ctx, bop);
    sync_completion_wait(&cplt, ZX_TIME_INFINITE);

    if (bop->command != ZX_OK) {
        zxlogf(ERROR, "mbr: could not read mbr from device, retcode = %d\n", bop->command);
        goto unbind;
    }

    uint8_t buffer[MBR_SIZE];
    mbr_t* mbr = (mbr_t*)buffer;
    if (vmo_read(vmo, buffer, 0, MBR_SIZE) != ZX_OK) {
        goto unbind;
    }

    // Validate the MBR boot signature.
    if (mbr->boot_signature != MBR_BOOT_SIGNATURE) {
        zxlogf(ERROR, "mbr: invalid mbr boot signature, expected 0x%04x got 0x%04x\n",
               MBR_BOOT_SIGNATURE, mbr->boot_signature);
        goto unbind;
    }

    // Parse the partitions out of the MBR.
    for (; partition_count < MBR_NUM_PARTITIONS; partition_count++) {
        mbr_partition_entry_t* entry = &mbr->partition[partition_count];
        if (entry->type == PARTITION_TYPE_NONE) {
            // This partition entry is empty and does not refer to a partition,
            // skip it.
            continue;
        }

        zxlogf(SPEW, "mbr: found partition, entry = %d, type = 0x%02x, "
               "start = %u, length = %u\n",
               partition_count + 1, entry->type, entry->start_sector_lba,
               entry->sector_partition_length);

        mbrpart_device_t* pdev;
        // use first_dev for first partition
        if (first_dev) {
            pdev = first_dev;
        } else {
            pdev = calloc(1, sizeof(*pdev));
            if (!pdev) {
                zxlogf(ERROR, "mbr: out of memory\n");
                goto unbind;
            }
            pdev->parent = dev;
            memcpy(&pdev->bp, &bp, sizeof(bp));
        }

        memcpy(&pdev->partition, entry, sizeof(*entry));
        block_info.block_count = pdev->partition.sector_partition_length;
        memcpy(&pdev->info, &block_info, sizeof(block_info));
        pdev->block_op_size = block_op_size;

        if (first_dev) {
            // make our initial device visible and use if for partition zero
            device_make_visible(first_dev->zxdev);
            first_dev = NULL;
        } else {
            char name[16];
            snprintf(name, sizeof(name), "part-%03u",partition_count);

            device_add_args_t args = {
                .version = DEVICE_ADD_ARGS_VERSION,
                .name = name,
                .ctx = pdev,
                .ops = &mbr_proto,
                .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
                .proto_ops = &block_ops,
            };

            if (device_add(dev, &args, &pdev->zxdev) != ZX_OK) {
                free(pdev);
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

static zx_status_t mbr_bind(void* ctx, zx_device_t* parent) {
    // Make sure the MBR structs are the right size.
    static_assert(sizeof(mbr_t) == MBR_SIZE, "mbr_t is the wrong size");
    static_assert(sizeof(mbr_partition_entry_t) == MBR_PARTITION_ENTRY_SIZE,
                  "mbr_partition_entry_t is the wrong size");

    // create an invisible device, which will be used for the first partition
    mbrpart_device_t* device = calloc(1, sizeof(mbrpart_device_t));
    if (!device) {
        return ZX_ERR_NO_MEMORY;
    }
    device->parent = parent;

    if (device_get_protocol(parent, ZX_PROTOCOL_BLOCK, &device->bp) != ZX_OK) {
        zxlogf(ERROR, "mbr: ERROR: block device '%s': does not support block protocol\n",
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
        .ops = &mbr_proto,
        .proto_id = ZX_PROTOCOL_BLOCK_IMPL,
        .proto_ops = &block_ops,
        .flags = DEVICE_ADD_INVISIBLE,
    };

    zx_status_t status = device_add(parent, &args, &device->zxdev);
    if (status != ZX_OK) {
        free(device);
        return status;
    }

    // Read the partition table asyncrhonously.
    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, mbr_bind_thread, device, "mbr-init");
    if (thrd_rc != thrd_success) {
        return thrd_status_to_zx_status(thrd_rc);
    }
    return ZX_OK;
}

static zx_driver_ops_t mbr_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = mbr_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(mbr, mbr_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK),
ZIRCON_DRIVER_END(mbr)
// clang-format on
