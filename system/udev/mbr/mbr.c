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

typedef struct mbr_bind_info {
    mx_device_t* dev;
    mx_driver_t* drv;
} mbr_bind_info_t;

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
    mx_device_t device;
    mbr_partition_entry_t partition;
    uint64_t blksiz;
    atomic_int writercount;
} mbrpart_device_t;

#define get_mbrpart_device(dev) containerof(dev, mbrpart_device_t, device)

static void mbr_read_sync_complete(iotxn_t* txn, void* cookie) {
    // Used to synchronize iotxn_calls.
    completion_signal((completion_t*)cookie);
}

static inline bool is_writer(uint32_t flags) {
    return (flags & O_RDWR || flags & O_WRONLY);
}

static uint64_t getsize(mbrpart_device_t* dev) {
    // Returns the size of the partition referred to by dev.
    return dev->partition.sector_partition_length * dev->blksiz;
}

static ssize_t mbr_ioctl(mx_device_t* dev, uint32_t op, const void* cmd,
                         size_t cmdlen, void* reply, size_t max) {
    mbrpart_device_t* device = get_mbrpart_device(dev);
    switch (op) {
    case IOCTL_BLOCK_GET_SIZE: {
        uint64_t* size = reply;
        if (max < sizeof(*size))
            return ERR_BUFFER_TOO_SMALL;
        *size = getsize(device);
        return sizeof(*size);
    }
    case IOCTL_BLOCK_GET_BLOCKSIZE: {
        uint64_t* blksize = reply;
        if (max < sizeof(*blksize))
            return ERR_BUFFER_TOO_SMALL;
        *blksize = device->blksiz;
        return sizeof(*blksize);
    }
    case IOCTL_BLOCK_GET_TYPE_GUID: {
        ssize_t retval = ERR_NOT_FOUND;
        char* guid = reply;
        if (max < GPT_GUID_LEN)
            return ERR_BUFFER_TOO_SMALL;
        if (device->partition.type == PARTITION_TYPE_DATA) {
            memcpy(guid, data_guid, GPT_GUID_LEN);
            retval = GPT_GUID_LEN;
        } else if (device->partition.type == PARTITION_TYPE_SYS) {
            memcpy(guid, sys_guid, GPT_GUID_LEN);
            retval = GPT_GUID_LEN;
        }
        return retval;
    }
    case IOCTL_BLOCK_GET_PARTITION_GUID: {
        return ERR_NOT_SUPPORTED;
    }
    case IOCTL_BLOCK_GET_NAME: {
        char* name = reply;
        memset(name, 0, max);
        strncpy(name, dev->name, max);
        return strnlen(name, max);
    }
    case IOCTL_DEVICE_SYNC: {
        // Propagate sync to parent device
        return dev->parent->ops->ioctl(dev->parent, IOCTL_DEVICE_SYNC, NULL, 0,
                                       NULL, 0);
    }
    default:
        return ERR_NOT_SUPPORTED;
    }
    return 0;
}

static void mbr_iotxn_queue(mx_device_t* dev, iotxn_t* txn) {

    // Sanity check to ensure that we're not writing past
    mbrpart_device_t* device = get_mbrpart_device(dev);

    const uint64_t off_lba = txn->offset / device->blksiz;
    const uint64_t first = device->partition.start_sector_lba;
    const uint64_t last = first + device->partition.sector_partition_length;

    // Offset can be in the range [first, last)
    if (first + off_lba >= last) {
        xprintf("mbr: %s offset 0x%" PRIx64 " is past the end of partition!\n",
                dev->name, txn->offset);
        iotxn_complete(txn, ERR_INVALID_ARGS, 0);
        return;
    }

    // Truncate the read if too many bytes are requested.
    txn->length = MIN((last - (first + off_lba)) * device->blksiz,
                      txn->length);

    // Move the offset to the start of the partition when forwarding this
    // request to the block device.
    txn->offset = first * device->blksiz + txn->offset;
    iotxn_queue(dev->parent, txn);
}

static mx_off_t mbr_getsize(mx_device_t* dev) {
    return getsize(get_mbrpart_device(dev));
}

static void mbr_unbind(mx_device_t* dev) {
    mbrpart_device_t* device = get_mbrpart_device(dev);
    device_remove(&device->device);
}

static mx_status_t mbr_release(mx_device_t* dev) {
    mbrpart_device_t* device = get_mbrpart_device(dev);
    free(device);
    return NO_ERROR;
}

static mx_status_t mbr_open(mx_device_t* dev, mx_device_t** dev_out,
                            uint32_t flags) {
    mbrpart_device_t* device = get_mbrpart_device(dev);
    mx_status_t status = NO_ERROR;
    if (is_writer(flags) && (atomic_exchange(&device->writercount, 1) == 1)) {
        xprintf("Partition cannot be opened as writable (open elsewhere)\n");
        status = ERR_ALREADY_BOUND;
    }
    return status;
}

static mx_status_t mbr_close(mx_device_t* dev, uint32_t flags) {
    mbrpart_device_t* device = get_mbrpart_device(dev);
    if (is_writer(flags)) {
        atomic_fetch_sub(&device->writercount, 1);
    }
    return NO_ERROR;
}

static mx_protocol_device_t mbr_proto = {
    .ioctl = mbr_ioctl,
    .iotxn_queue = mbr_iotxn_queue,
    .get_size = mbr_getsize,
    .unbind = mbr_unbind,
    .release = mbr_release,
    .open = mbr_open,
    .close = mbr_close,
};

static int mbr_bind_thread(void* arg) {
    mbr_bind_info_t* info = (mbr_bind_info_t*)arg;
    mx_device_t* dev = info->dev;
    mx_driver_t* drv = info->drv;
    free(info);

    // Classic MBR supports 4 partitions.
    uint8_t partition_count = 0;
    uint64_t blksiz;
    iotxn_t* txn = NULL;

    ssize_t rc = dev->ops->ioctl(dev, IOCTL_BLOCK_GET_BLOCKSIZE, NULL, 0,
                                 &blksiz, sizeof(blksiz));
    if (rc < 0) {
        xprintf("mbr: Could not get block size for dev=%s, retcode = %zd\n",
                dev->name, rc);
        goto unbind;
    }

    // We need to read at least 512B to parse the MBR. Determine if we should
    // read the device's block size or we should ready exactly 512B.
    size_t iotxn_size = 0;
    if (blksiz >= MBR_SIZE) {
        iotxn_size = blksiz;
    } else {
        // Make sure we're reading some multiple of the block size.
        iotxn_size = DIV_ROUND_UP(MBR_SIZE, blksiz) * blksiz;
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

        char name[128];
        snprintf(name, sizeof(name), "%sp%u", dev->name, partition_count);
        device_init(&pdev->device, drv, name, &mbr_proto);

        pdev->device.protocol_id = MX_PROTOCOL_BLOCK;
        pdev->blksiz = blksiz;
        memcpy(&pdev->partition, entry, sizeof(*entry));

        if ((st = device_add(&pdev->device, dev)) != NO_ERROR) {
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
        driver_unbind(drv, dev);
    }

    return -1;
}

static mx_status_t mbr_bind(mx_driver_t* drv, mx_device_t* dev, void** cookie) {
    // Make sure the MBR structs are the right size.
    static_assert(sizeof(mbr_t) == MBR_SIZE, "mbr_t is the wrong size");
    static_assert(sizeof(mbr_partition_entry_t) == MBR_PARTITION_ENTRY_SIZE,
                  "mbr_partition_entry_t is the wrong size");

    mbr_bind_info_t* info = malloc(sizeof(*info));
    if (!info)
        return ERR_NO_MEMORY;

    info->drv = drv;
    info->dev = dev;

    // Read the partition table asyncrhonously.
    thrd_t t;
    int thrd_rc = thrd_create_with_name(&t, mbr_bind_thread, info,
                                        "mbr-init");
    if (thrd_rc != thrd_success) {
        free(info);
        return thrd_status_to_mx_status(thrd_rc);
    }
    return NO_ERROR;
}

mx_driver_t _driver_mbr = {
    .ops = {
        .bind = mbr_bind,
    },
    // Don't automatically bind this driver, instead let the FS layer select
    // this driver if a block device with an MBR is detected.
};

// clang-format off
MAGENTA_DRIVER_BEGIN(_driver_mbr, "mbr", "magenta", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, MX_PROTOCOL_BLOCK),
MAGENTA_DRIVER_END(_driver_mbr)
// clang-format on
