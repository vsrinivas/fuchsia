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
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>

#include <magenta/threads.h>
#include <sync/completion.h>

#include <gpt/gpt.h>

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))
#define MBR_SIZE 512
#define MBR_PARTITION_ENTRY_SIZE 16
#define MBR_NUM_PARTITIONS 4
#define MBR_BOOT_SIGNATURE 0xAA55

#define TRACE 0

#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

// ATTN: MBR supports 8 bit partition types instead of GUIDs. Here we define
// mappings between partition type and GUIDs that magenta understands. When
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
    mx_device_t* mxdev;
    mx_device_t* parent;
    mbr_partition_entry_t partition;
    block_info_t info;
    block_callbacks_t* callbacks;
    atomic_int writercount;
} mbrpart_device_t;


static void mbr_read_sync_complete(iotxn_t* txn, void* cookie) {
    // Used to synchronize iotxn_calls.
    completion_signal((completion_t*)cookie);
}

static inline bool is_writer(uint32_t flags) {
    return (flags & O_RDWR || flags & O_WRONLY);
}

static uint64_t getsize(mbrpart_device_t* dev) {
    // Returns the size of the partition referred to by dev.
    return dev->partition.sector_partition_length * ((uint64_t) dev->info.block_size);
}

static mx_status_t mbr_ioctl(void* ctx, uint32_t op, const void* cmd,
                             size_t cmdlen, void* reply, size_t max,
                             size_t* out_actual) {
    mbrpart_device_t* device = ctx;
    switch (op) {
    case IOCTL_BLOCK_GET_INFO: {
        block_info_t* info = reply;
        if (max < sizeof(*info))
            return ERR_BUFFER_TOO_SMALL;
        memcpy(info, &device->info, sizeof(*info));
        *out_actual = sizeof(*info);
        return NO_ERROR;
    }
    case IOCTL_BLOCK_GET_TYPE_GUID: {
        char* guid = reply;
        if (max < GPT_GUID_LEN)
            return ERR_BUFFER_TOO_SMALL;
        if (device->partition.type == PARTITION_TYPE_DATA) {
            memcpy(guid, data_guid, GPT_GUID_LEN);
            *out_actual = GPT_GUID_LEN;
            return NO_ERROR;
        } else if (device->partition.type == PARTITION_TYPE_SYS) {
            memcpy(guid, sys_guid, GPT_GUID_LEN);
            *out_actual = GPT_GUID_LEN;
            return NO_ERROR;
        } else {
            return ERR_NOT_FOUND;
        }
    }
    case IOCTL_BLOCK_GET_PARTITION_GUID: {
        return ERR_NOT_SUPPORTED;
    }
    case IOCTL_BLOCK_GET_NAME: {
        char* name = reply;
        memset(name, 0, max);
        strncpy(name, device_get_name(device->mxdev), max);
        *out_actual = strnlen(name, max);
        return NO_ERROR;
    }
    case IOCTL_DEVICE_SYNC: {
        // Propagate sync to parent device
        return device_op_ioctl(device->parent, IOCTL_DEVICE_SYNC, NULL, 0, NULL, 0, NULL);
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
}

static mx_off_t to_parent_offset(mbrpart_device_t* dev, mx_off_t offset) {
    return offset + (uint64_t)(dev->partition.start_sector_lba) *
           (uint64_t)dev->info.block_size;
}

static void mbr_iotxn_queue(void* ctx, iotxn_t* txn) {
    mbrpart_device_t* dev = ctx;
    if (txn->offset % dev->info.block_size) {
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }
    if (txn->offset > getsize(dev)) {
        iotxn_complete(txn, ERR_OUT_OF_RANGE, 0);
        return;
    }
    // transactions from read()/write() may be truncated
    txn->length = ROUNDDOWN(txn->length, dev->info.block_size);
    txn->length = MIN(txn->length, getsize(dev) - txn->offset);
    txn->offset = to_parent_offset(dev, txn->offset);
    if (txn->length == 0) {
        iotxn_complete(txn, NO_ERROR, 0);
    } else {
        iotxn_queue(dev->parent, txn);
    }
}

static mx_off_t mbr_getsize(void* ctx) {
    return getsize(ctx);
}

static void mbr_unbind(void* ctx) {
    mbrpart_device_t* device = ctx;
    device_remove(device->mxdev);
}

static void mbr_release(void* ctx) {
    mbrpart_device_t* device = ctx;
    free(device);
}

static mx_status_t mbr_open(void* ctx, mx_device_t** dev_out,
                            uint32_t flags) {
    mbrpart_device_t* device = ctx;
    mx_status_t status = NO_ERROR;
    if (is_writer(flags) && (atomic_exchange(&device->writercount, 1) == 1)) {
        xprintf("Partition cannot be opened as writable (open elsewhere)\n");
        status = ERR_ALREADY_BOUND;
    }
    return status;
}

static mx_status_t mbr_close(void* ctx, uint32_t flags) {
    mbrpart_device_t* device = ctx;
    if (is_writer(flags)) {
        atomic_fetch_sub(&device->writercount, 1);
    }
    return NO_ERROR;
}

static mx_protocol_device_t mbr_proto = {
    .version = DEVICE_OPS_VERSION,
    .ioctl = mbr_ioctl,
    .iotxn_queue = mbr_iotxn_queue,
    .get_size = mbr_getsize,
    .unbind = mbr_unbind,
    .release = mbr_release,
    .open = mbr_open,
    .close = mbr_close,
};

static void mbr_block_set_callbacks(mx_device_t* dev, block_callbacks_t* cb) {
    mbrpart_device_t* device = dev->ctx;
    device->callbacks = cb;
}

static void mbr_block_get_info(mx_device_t* dev, block_info_t* info) {
    mbrpart_device_t* device = dev->ctx;
    memcpy(info, &device->info, sizeof(*info));
}

static void mbr_block_complete(iotxn_t* txn, void* cookie) {
    mbrpart_device_t* dev;
    memcpy(&dev, txn->extra, sizeof(mbrpart_device_t*));
    dev->callbacks->complete(cookie, txn->status);
    iotxn_release(txn);
}

static void block_do_txn(mbrpart_device_t* dev, uint32_t opcode, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    block_info_t* info = &dev->info;
    if ((dev_offset % info->block_size) || (length % info->block_size)) {
        dev->callbacks->complete(cookie, ERR_INVALID_ARGS);
        return;
    }
    uint64_t size = getsize(dev);
    if ((dev_offset >= size) || (length >= (size - dev_offset))) {
        dev->callbacks->complete(cookie, ERR_OUT_OF_RANGE);
        return;
    }

    mx_status_t status;
    iotxn_t* txn;
    if ((status = iotxn_alloc_vmo(&txn, IOTXN_ALLOC_POOL, vmo, vmo_offset, length)) != NO_ERROR) {
        dev->callbacks->complete(cookie, status);
        return;
    }
    txn->opcode = opcode;
    txn->length = length;
    txn->offset = to_parent_offset(dev, dev_offset);
    txn->complete_cb = mbr_block_complete;
    txn->cookie = cookie;
    memcpy(txn->extra, &dev, sizeof(mbrpart_device_t*));
    iotxn_queue(dev->parent, txn);
}

static void mbr_block_read(mx_device_t* dev, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    block_do_txn((mbrpart_device_t*)dev->ctx, IOTXN_OP_READ, vmo, length, vmo_offset, dev_offset, cookie);
}

static void mbr_block_write(mx_device_t* dev, mx_handle_t vmo, uint64_t length, uint64_t vmo_offset, uint64_t dev_offset, void* cookie) {
    block_do_txn((mbrpart_device_t*)dev->ctx, IOTXN_OP_WRITE, vmo, length, vmo_offset, dev_offset, cookie);
}

static block_ops_t mbr_block_ops = {
    .set_callbacks = mbr_block_set_callbacks,
    .get_info = mbr_block_get_info,
    .read = mbr_block_read,
    .write = mbr_block_write,
};

static int mbr_bind_thread(void* arg) {
    mx_device_t* dev = arg;

    // Classic MBR supports 4 partitions.
    uint8_t partition_count = 0;
    iotxn_t* txn = NULL;

    block_info_t block_info;
    size_t actual;
    ssize_t rc = device_op_ioctl(dev, IOCTL_BLOCK_GET_INFO, NULL, 0,
                                 &block_info, sizeof(block_info), &actual);
    if (rc < 0 || actual != sizeof(block_info)) {
        xprintf("mbr: Could not get block size for dev=%s, retcode = %zd\n",
                dev->name, rc);
        goto unbind;
    }

    // We need to read at least 512B to parse the MBR. Determine if we should
    // read the device's block size or we should ready exactly 512B.
    size_t iotxn_size = 0;
    if (block_info.block_size >= MBR_SIZE) {
        iotxn_size = block_info.block_size;
    } else {
        // Make sure we're reading some multiple of the block size.
        iotxn_size = DIV_ROUND_UP(MBR_SIZE, block_info.block_size) * block_info.block_size;
    }

    mx_status_t st = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS, iotxn_size);
    if (st != NO_ERROR) {
        xprintf("mbr: failed to allocate iotxn, retcode = %d\n", st);
        goto unbind;
    }

    completion_t cplt = COMPLETION_INIT;
    txn->opcode = IOTXN_OP_READ;
    txn->offset = 0;
    txn->length = iotxn_size;
    txn->complete_cb = mbr_read_sync_complete;
    txn->cookie = &cplt;

    iotxn_queue(dev, txn);
    completion_wait(&cplt, MX_TIME_INFINITE);

    if (txn->status != NO_ERROR) {
        xprintf("mbr: could not read mbr from device, retcode = %d\n",
                txn->status);
        goto unbind;
    }

    if (txn->actual < MBR_SIZE) {
        xprintf("mbr: expected to read %u bytes but only read %" PRIu64 "\n",
                MBR_SIZE, txn->actual);
        goto unbind;
    }

    uint8_t buffer[MBR_SIZE];
    iotxn_copyfrom(txn, buffer, MBR_SIZE, 0);
    mbr_t* mbr = (mbr_t*)buffer;

    // Validate the MBR boot signature.
    if (mbr->boot_signature != MBR_BOOT_SIGNATURE) {
        xprintf("mbr: invalid mbr boot signature, expected 0x%04x got 0x%04x\n",
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

        xprintf("mbr: found partition, entry = %d, type = 0x%02x, "
                "start = %u, length = %u\n",
                partition_count + 1, entry->type, entry->start_sector_lba,
                entry->sector_partition_length);

        mbrpart_device_t* pdev = calloc(1, sizeof(*pdev));
        if (!pdev) {
            xprintf("mbr: out of memory\n");
            goto unbind;
        }
        pdev->parent = dev;

        memcpy(&pdev->partition, entry, sizeof(*entry));
        block_info.block_count = pdev->partition.sector_partition_length;
        memcpy(&pdev->info, &block_info, sizeof(block_info));

        char name[128];
        snprintf(name, sizeof(name), "%sp%u", device_get_name(dev), partition_count);

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = name,
            .ctx = pdev,
            .ops = &mbr_proto,
            .proto_id = MX_PROTOCOL_BLOCK_CORE,
            .proto_ops = &mbr_block_ops,
        };

        if ((st = device_add(dev, &args, &pdev->mxdev)) != NO_ERROR) {
            xprintf("mbr: device_add failed, retcode = %d\n", st);
            free(pdev);
            continue;
        }
    }

    iotxn_release(txn);

    return 0;
unbind:
    if (txn)
        iotxn_release(txn);

    // If we weren't able to bind any subdevices (for partitions), then unbind
    // the MBR driver as well.
    if (partition_count == 0) {
        device_unbind(dev);
    }

    return -1;
}

static mx_status_t mbr_bind(void* ctx, mx_device_t* dev, void** cookie) {
    // Make sure the MBR structs are the right size.
    static_assert(sizeof(mbr_t) == MBR_SIZE, "mbr_t is the wrong size");
    static_assert(sizeof(mbr_partition_entry_t) == MBR_PARTITION_ENTRY_SIZE,
                  "mbr_partition_entry_t is the wrong size");

    // Read the partition table asyncrhonously.
    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, mbr_bind_thread, dev, "mbr-init");
    if (thrd_rc != thrd_success) {
        return thrd_status_to_mx_status(thrd_rc);
    }
    return NO_ERROR;
}

static mx_driver_ops_t mbr_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = mbr_bind,
};

// clang-format off
MAGENTA_DRIVER_BEGIN(mbr, mbr_driver_ops, "magenta", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK),
MAGENTA_DRIVER_END(mbr)
// clang-format on
