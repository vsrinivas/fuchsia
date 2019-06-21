// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpt.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/block/partition.h>
#include <lib/cksum.h>
#include <lib/sync/completion.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

namespace {

typedef gpt_header_t gpt_t;

constexpr size_t kTransactionSize = 0x4000; // 128 partition entries

struct Guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

struct gptpart_device_t {
    zx_device_t* zxdev;
    zx_device_t* parent;

    block_impl_protocol_t block_protocol;

    gpt_entry_t gpt_entry;

    block_info_t info;
    size_t block_op_size;
};

class ThreadArgs {
public:
    static zx_status_t CreateThreadArgs(zx_device_t* parent, gptpart_device_t* first_device,
                                        std::unique_ptr<ThreadArgs>* out);

    ThreadArgs() = delete;
    ThreadArgs(const ThreadArgs&) = delete;
    ThreadArgs(ThreadArgs&&) = delete;
    ThreadArgs& operator=(const ThreadArgs&) = delete;
    ThreadArgs& operator=(ThreadArgs&&) = delete;
    ~ThreadArgs() = default;

    gptpart_device_t* first_device() const { return first_device_; }
    const guid_map_t* guid_map() const { return guid_map_; }
    uint64_t guid_map_entries() const { return guid_map_entries_; }

private:
    explicit ThreadArgs(gptpart_device_t* first_device) : first_device_(first_device) {}

    gptpart_device_t* first_device_ = {};
    guid_map_t guid_map_[DEVICE_METADATA_GUID_MAP_MAX_ENTRIES] = {};
    uint64_t guid_map_entries_ = {};
};

zx_status_t ThreadArgs::CreateThreadArgs(zx_device_t* parent, gptpart_device_t* first_device,
                                         std::unique_ptr<ThreadArgs>* out) {
    size_t actual;
    auto thread_args = std::unique_ptr<ThreadArgs>(new ThreadArgs(first_device));

    if (thread_args.get() == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status =
        device_get_metadata(parent, DEVICE_METADATA_GUID_MAP, thread_args->guid_map_,
                            sizeof(thread_args->guid_map_), &actual);
    // TODO(ZX-4219): We should not continue loading the driver here. Upper layer
    //                may rely on guid to take action on a partition.
    if (status != ZX_OK) {
        zxlogf(INFO, "gpt: device_get_metadata failed (%d)\n", status);
    } else if (actual % sizeof(*thread_args->guid_map_) != 0) {
        zxlogf(INFO, "gpt: GUID map size is invalid (%lu)\n", actual);
    } else {
        thread_args->guid_map_entries_ = actual / sizeof(thread_args->guid_map_[0]);
    }

    *out = std::move(thread_args);
    return ZX_OK;
}

void uint8_to_guid_string(char* dst, uint8_t* src) {
    Guid* guid = (Guid*)src;
    sprintf(dst, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", guid->data1, guid->data2,
            guid->data3, guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
            guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}

// TODO(ZX-3241): Ensure the output string of this function is always null-terminated.
void utf16_to_cstring(char* dst, uint8_t* src, size_t charcount) {
    while (charcount > 0) {
        *dst++ = *src;
        src += 2; // FIXME cheesy
        charcount -= 2;
    }
}

uint64_t get_lba_count(gptpart_device_t* dev) {
    // last LBA is inclusive
    return dev->gpt_entry.last - dev->gpt_entry.first + 1;
}

bool validate_header(const gpt_t* header, const block_info_t* info) {
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
    if (header->entries_count * header->entries_size > kTransactionSize) {
        zxlogf(ERROR, "gpt: entry table too big\n");
        return false;
    }
    return true;
}

void apply_guid_map(const guid_map_t* guid_map, size_t entries,
                    const char* name, uint8_t* type) {
    for (size_t i = 0; i < entries; i++) {
        if (strncmp(name, guid_map[i].name, GPT_NAME_LEN) == 0) {
            memcpy(type, guid_map[i].guid, GPT_GUID_LEN);
            return;
        }
    }
}

// implement device protocol:
void gpt_query(void* ctx, block_info_t* bi, size_t* bopsz) {
    gptpart_device_t* gpt = static_cast<gptpart_device_t*>(ctx);
    memcpy(bi, &gpt->info, sizeof(block_info_t));
    *bopsz = gpt->block_op_size;
}

void gpt_queue(void* ctx, block_op_t* bop, block_impl_queue_callback completion_cb, void* cookie) {
    gptpart_device_t* gpt = static_cast<gptpart_device_t*>(ctx);

    switch (bop->command & BLOCK_OP_MASK) {
    case BLOCK_OP_READ:
    case BLOCK_OP_WRITE: {
        size_t blocks = bop->rw.length;
        size_t max = get_lba_count(gpt);

        // Ensure that the request is in-bounds
        if ((bop->rw.offset_dev >= max) ||
            ((max - bop->rw.offset_dev) < blocks)) {
            completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, bop);
            return;
        }

        // Adjust for partition starting block
        bop->rw.offset_dev += gpt->gpt_entry.first;
        break;
    }
    case BLOCK_OP_FLUSH:
        break;
    default:
        completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, bop);
        return;
    }

    block_impl_queue(&gpt->block_protocol, bop, completion_cb, cookie);
}

void gpt_unbind(void* ctx) {
    gptpart_device_t* device = static_cast<gptpart_device_t*>(ctx);
    device_remove(device->zxdev);
}

void gpt_release(void* ctx) {
    gptpart_device_t* device = static_cast<gptpart_device_t*>(ctx);
    free(device);
}

zx_off_t gpt_get_size(void* ctx) {
    gptpart_device_t* dev = static_cast<gptpart_device_t*>(ctx);
    return dev->info.block_count * dev->info.block_size;
}

block_impl_protocol_ops_t block_ops = {
    .query = gpt_query,
    .queue = gpt_queue,
};

static_assert(GPT_GUID_LEN == GUID_LENGTH, "GUID length mismatch");

zx_status_t gpt_get_guid(void* ctx, guidtype_t guidtype, guid_t* out_guid) {
    gptpart_device_t* device = static_cast<gptpart_device_t*>(ctx);
    switch (guidtype) {
    case GUIDTYPE_TYPE:
        memcpy(out_guid, device->gpt_entry.type, GPT_GUID_LEN);
        return ZX_OK;
    case GUIDTYPE_INSTANCE:
        memcpy(out_guid, device->gpt_entry.guid, GPT_GUID_LEN);
        return ZX_OK;
    default:
        return ZX_ERR_INVALID_ARGS;
    }
}

static_assert(GPT_NAME_LEN <= MAX_PARTITION_NAME_LENGTH, "Partition name length mismatch");

zx_status_t gpt_get_name(void* ctx, char* out_name, size_t capacity) {
    if (capacity < GPT_NAME_LEN) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    gptpart_device_t* device = static_cast<gptpart_device_t*>(ctx);
    memset(out_name, 0, GPT_NAME_LEN);
    utf16_to_cstring(out_name, device->gpt_entry.name, GPT_NAME_LEN);
    return ZX_OK;
}

block_partition_protocol_ops_t partition_ops = {
    .get_guid = gpt_get_guid,
    .get_name = gpt_get_name,
};

zx_status_t gpt_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    gptpart_device_t* device = static_cast<gptpart_device_t*>(ctx);
    switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL: {
        auto protocol = static_cast<block_impl_protocol_t*>(out);
        protocol->ctx = device;
        protocol->ops = &block_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_BLOCK_PARTITION: {
        auto protocol = static_cast<block_partition_protocol_t*>(out);
        protocol->ctx = device;
        protocol->ops = &partition_ops;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_protocol_device_t gpt_proto = []() {
    zx_protocol_device_t gpt = {};
    gpt.version = DEVICE_OPS_VERSION;
    gpt.get_protocol = gpt_get_protocol;
    gpt.unbind = gpt_unbind;
    gpt.release = gpt_release;
    gpt.get_size = gpt_get_size;
    return gpt;
}();

void gpt_read_sync_complete(void* cookie, zx_status_t status, block_op_t* bop) {
    // Pass 32bit status back to caller via 32bit command field
    // Saves from needing custom structs, etc.
    bop->command = status;
    sync_completion_signal((sync_completion_t*)cookie);
}

zx_status_t vmo_read(zx_handle_t vmo, void* data, uint64_t off, size_t len) {
    return zx_vmo_read(vmo, data, off, len);
}

int gpt_bind_thread(void* arg) {
    std::unique_ptr<ThreadArgs> thread_args(static_cast<ThreadArgs*>(arg));
    gptpart_device_t* first_dev = thread_args->first_device();
    zx_device_t* dev = first_dev->parent;

    const guid_map_t* guid_map = thread_args->guid_map();
    uint64_t guid_map_entries = thread_args->guid_map_entries();

    // used to keep track of number of partitions found
    unsigned partitions = 0;
    uint64_t dev_block_count;
    uint64_t length;
    uint32_t crc;
    size_t table_sz;
    sync_completion_t completion;

    block_impl_protocol_t block_protocol;
    memcpy(&block_protocol, &first_dev->block_protocol, sizeof(block_protocol));

    block_info_t block_info;
    size_t block_op_size;
    block_protocol.ops->query(block_protocol.ctx, &block_info, &block_op_size);

    zx_handle_t vmo = ZX_HANDLE_INVALID;
    block_op_t* bop = static_cast<block_op_t*>(calloc(1, block_op_size));
    if (bop == NULL) {
        goto unbind;
    }

    if (zx_vmo_create(kTransactionSize, 0, &vmo) != ZX_OK) {
        zxlogf(ERROR, "gpt: cannot allocate vmo\n");
        goto unbind;
    }

    // sanity check the default txn size with the block size
    if ((kTransactionSize % block_info.block_size) || (kTransactionSize < block_info.block_size)) {
        zxlogf(ERROR, "gpt: default txn size=%lu is not aligned to blksize=%u!\n",
               kTransactionSize, block_info.block_size);
        goto unbind;
    }

    // read partition table header synchronously (LBA1)
    bop->command = BLOCK_OP_READ;
    bop->rw.vmo = vmo;
    bop->rw.length = 1;
    bop->rw.offset_dev = 1;
    bop->rw.offset_vmo = 0;

    block_protocol.ops->queue(block_protocol.ctx, bop, gpt_read_sync_complete, &completion);
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
    table_sz = header.entries_count * header.entries_size;
    if (table_sz > kTransactionSize) {
        zxlogf(INFO, "gpt: partition table is larger than the buffer!\n");
        // FIXME read the whole partition table. ok for now because on pixel2, this is
        // enough to read the entries that actually contain valid data
        table_sz = kTransactionSize;
    }

    bop->command = BLOCK_OP_READ;
    bop->rw.vmo = vmo;
    length = (table_sz + (block_info.block_size - 1)) / block_info.block_size;
    ZX_DEBUG_ASSERT(length <= UINT32_MAX);
    bop->rw.length = static_cast<uint32_t>(length);
    bop->rw.offset_dev = header.entries;
    bop->rw.offset_vmo = 0;

    sync_completion_reset(&completion);
    block_protocol.ops->queue(block_protocol.ctx, bop, gpt_read_sync_complete, &completion);
    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    if (bop->command != ZX_OK) {
        zxlogf(ERROR, "gpt: error %d reading partition table\n", bop->command);
        goto unbind;
    }

    uint8_t entries[kTransactionSize];
    if (vmo_read(vmo, entries, 0, kTransactionSize) != ZX_OK) {
        goto unbind;
    }

    crc = crc32(0, static_cast<const unsigned char*>(entries), table_sz);
    if (crc != header.entries_crc) {
        zxlogf(ERROR, "gpt: entries crc invalid\n");
        goto unbind;
    }

    dev_block_count = block_info.block_count;

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
            device = static_cast<gptpart_device_t*>(calloc(1, sizeof(gptpart_device_t)));
            if (!device) {
                zxlogf(ERROR, "gpt: out of memory!\n");
                goto unbind;
            }
            device->parent = dev;
            memcpy(&device->block_protocol, &block_protocol, sizeof(block_protocol));
        }

        memcpy(&device->gpt_entry, entry, sizeof(gpt_entry_t));
        block_info.block_count = device->gpt_entry.last - device->gpt_entry.first + 1;
        memcpy(&device->info, &block_info, sizeof(block_info));
        device->block_op_size = block_op_size;

        char partition_guid[GPT_GUID_STRLEN];
        uint8_to_guid_string(partition_guid, device->gpt_entry.guid);
        char pname[GPT_NAME_LEN];
        utf16_to_cstring(pname, device->gpt_entry.name, GPT_NAME_LEN);

        apply_guid_map(guid_map, guid_map_entries, pname, device->gpt_entry.type);

        char type_guid[GPT_GUID_STRLEN];
        uint8_to_guid_string(type_guid, device->gpt_entry.type);

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

            device_add_args_t args = {};
            args.version = DEVICE_ADD_ARGS_VERSION;
            args.name = name;
            args.ctx = device;
            args.ops = &gpt_proto;
            args.proto_id = ZX_PROTOCOL_BLOCK_IMPL;
            args.proto_ops = &block_ops;

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

zx_status_t gpt_bind(void* ctx, zx_device_t* parent) {
    // create an invisible device, which will be used for the first partition
    gptpart_device_t* device = static_cast<gptpart_device_t*>(calloc(1, sizeof(gptpart_device_t)));
    if (!device) {
        return ZX_ERR_NO_MEMORY;
    }
    device->parent = parent;

    std::unique_ptr<ThreadArgs> thread_args;

    if (device_get_protocol(parent, ZX_PROTOCOL_BLOCK, &device->block_protocol) != ZX_OK) {
        zxlogf(ERROR, "gpt: ERROR: block device '%s': does not support block protocol\n",
               device_get_name(parent));
        free(device);
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = ThreadArgs::CreateThreadArgs(parent, device, &thread_args);
    if (status != ZX_OK) {
        free(device);
        return status;
    }

    char name[128];
    snprintf(name, sizeof(name), "part-%03u", 0);

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = name;
    args.ctx = device;
    args.ops = &gpt_proto;
    args.proto_id = ZX_PROTOCOL_BLOCK_IMPL;
    args.proto_ops = &block_ops;
    args.flags = DEVICE_ADD_INVISIBLE;

    status = device_add(parent, &args, &device->zxdev);
    if (status != ZX_OK) {
        free(device);
        return status;
    }

    // read partition table asynchronously
    thrd_t t;
    status = thrd_create_with_name(&t, gpt_bind_thread, thread_args.get(), "gpt-init");
    if (status != ZX_OK) {
        device_remove(device->zxdev);
    } else {
        thread_args.release();
    }
    return status;
}

constexpr zx_driver_ops_t gpt_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = gpt_bind;
    return ops;
}();

} // namespace

// clang-format off
ZIRCON_DRIVER_BEGIN(gpt, gpt_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK),
ZIRCON_DRIVER_END(gpt)
// clang-format on
