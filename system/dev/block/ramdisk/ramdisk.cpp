// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <memory>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/block.h>
#include <ddk/protocol/block/partition.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>
#include <zircon/device/block.h>
#include <zircon/device/ramdisk.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

namespace {

constexpr uint64_t kMaxTransferSize = 1LLU << 19;

typedef struct {
    zx_device_t* zxdev;
} ramctl_device_t;

typedef struct ramdisk_device {
    zx_device_t* zxdev;

    fzl::OwnedVmoMapper mapping;
    uint64_t block_size;
    uint64_t block_count;
    uint8_t type_guid[ZBI_PARTITION_GUID_LEN];

    // |signal| identifies when the worker thread should stop sleeping.
    // This may occur when the device:
    // - Is unbound,
    // - Received a message on a queue,
    // - Has |asleep| set to false.
    sync_completion_t signal;

    // Guards fields of the ramdisk which may be accessed concurrently
    // from a background worker thread.
    fbl::Mutex lock_;
    list_node_t txn_list TA_GUARDED(lock_);
    list_node_t deferred_list TA_GUARDED(lock_);

    // Identifies if the device has been unbound.
    bool dead TA_GUARDED(lock_);

    // Flags modified by RAMDISK_SET_FLAGS.
    //
    // Supported flags:
    // - RAMDISK_FLAG_RESUME_ON_WAKE: This flag identifies if requests which are
    // sent to the ramdisk while it is considered "alseep" should be processed
    // when the ramdisk wakes up. This is implemented by utilizing a "deferred
    // list" of requests, which are immediately re-issued on wakeup.
    uint32_t flags TA_GUARDED(lock_);

    // True if the ramdisk is "sleeping", and deferring all upcoming requests,
    // or dropping them if |RAMDISK_FLAG_RESUME_ON_WAKE| is not set.
    bool asleep TA_GUARDED(lock_);
    // The number of blocks-to-be-written that should be processed.
    // When this reaches zero, the ramdisk will set |asleep| to true.
    uint64_t pre_sleep_write_block_count TA_GUARDED(lock_);
    ramdisk_blk_counts_t block_counts TA_GUARDED(lock_);

    thrd_t worker;
    char name[ZBI_PARTITION_NAME_LEN];
} ramdisk_device_t;

typedef struct {
    block_op_t op;
    list_node_t node;
    block_impl_queue_callback completion_cb;
    void* cookie;
} ramdisk_txn_t;

// The worker thread processes messages from iotxns in the background
int worker_thread(void* arg) {
    zx_status_t status = ZX_OK;
    ramdisk_device_t* dev = (ramdisk_device_t*)arg;
    ramdisk_txn_t* txn = nullptr;
    bool dead, asleep, defer;
    uint64_t blocks = 0;

    for (;;) {
        for (;;) {
            {
                fbl::AutoLock lock(&dev->lock_);
                txn = nullptr;
                dead = dev->dead;
                asleep = dev->asleep;
                defer = (dev->flags & RAMDISK_FLAG_RESUME_ON_WAKE) != 0;
                blocks = dev->pre_sleep_write_block_count;

                if (!asleep) {
                    // If we are awake, try grabbing pending transactions from the deferred list.
                    txn = list_remove_head_type(&dev->deferred_list, ramdisk_txn_t, node);
                }

                if (txn == nullptr) {
                    // If no transactions were available in the deferred list (or we are asleep),
                    // grab one from the regular txn_list.
                    txn = list_remove_head_type(&dev->txn_list, ramdisk_txn_t, node);
                }
            }

            if (dead) {
                goto goodbye;
            }

            if (txn == nullptr) {
                sync_completion_wait(&dev->signal, ZX_TIME_INFINITE);
            } else {
                sync_completion_reset(&dev->signal);
                break;
            }
        }

        uint64_t txn_blocks = txn->op.rw.length;
        if (txn->op.command == BLOCK_OP_READ || blocks == 0 || blocks > txn_blocks) {
            // If the ramdisk is not configured to sleep after x blocks, or the number of blocks in
            // this transaction does not exceed the pre_sleep_write_block_count, or we are
            // performing a read operation, use the current transaction length.
            blocks = txn_blocks;
        }

        size_t length = blocks * dev->block_size;
        size_t dev_offset = txn->op.rw.offset_dev * dev->block_size;
        size_t vmo_offset = txn->op.rw.offset_vmo * dev->block_size;
        void* addr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(dev->mapping.start()) +
                                             dev_offset);

        if (length > kMaxTransferSize) {
            status = ZX_ERR_OUT_OF_RANGE;
        } else if (txn->op.command == BLOCK_OP_READ) {
            // A read operation should always succeed, even if the ramdisk is "asleep".
            status = zx_vmo_write(txn->op.rw.vmo, addr, vmo_offset, length);
        } else if (asleep) {
            if (defer) {
                // If we are asleep but resuming on wake, add txn to the deferred_list.
                // deferred_list is only accessed by the worker_thread, so a lock is not needed.
                list_add_tail(&dev->deferred_list, &txn->node);
                continue;
            } else {
                status = ZX_ERR_UNAVAILABLE;
            }
        } else { // BLOCK_OP_WRITE
            status = zx_vmo_read(txn->op.rw.vmo, addr, vmo_offset, length);

            if (status == ZX_OK && blocks < txn->op.rw.length && defer) {
                // If the first part of the transaction succeeded but the entire transaction is not
                // complete, we need to address the remainder.

                // If we are deferring after this block count, update the transaction to
                // reflect the blocks that have already been written, and add it to the
                // deferred queue.
                ZX_DEBUG_ASSERT_MSG(blocks <= std::numeric_limits<uint32_t>::max(),
                                    "Block count overflow");
                txn->op.rw.length -= static_cast<uint32_t>(blocks);
                txn->op.rw.offset_vmo += blocks;
                txn->op.rw.offset_dev += blocks;

                // Add the remaining blocks to the deferred list.
                list_add_tail(&dev->deferred_list, &txn->node);
            }
        }

        if (txn->op.command == BLOCK_OP_WRITE) {
            {
                // Update the ramdisk block counts. Since we aren't failing read transactions,
                // only include write transaction counts.
                fbl::AutoLock lock(&dev->lock_);
                // Increment the count based on the result of the last transaction.
                if (status == ZX_OK) {
                    dev->block_counts.successful += blocks;

                    if (blocks != txn_blocks && !defer) {
                        // If we are not deferring, then any excess blocks have failed.
                        dev->block_counts.failed += txn_blocks - blocks;
                        status = ZX_ERR_UNAVAILABLE;
                    }
                } else {
                    dev->block_counts.failed += txn_blocks;
                }

                // Put the ramdisk to sleep if we have reached the required # of blocks.
                if (dev->pre_sleep_write_block_count > 0) {
                    dev->pre_sleep_write_block_count -= blocks;
                    dev->asleep = (dev->pre_sleep_write_block_count == 0);
                }
            }

            if (defer && blocks != txn_blocks && status == ZX_OK) {
                // If we deferred partway through a transaction, hold off on returning the
                // result until the remainder of the transaction is completed.
                continue;
            }
        }

        if (txn->completion_cb) {
            txn->completion_cb(txn->cookie, status, &txn->op);
        }
    }

goodbye:
    while (txn != nullptr) {
        txn->completion_cb(txn->cookie, ZX_ERR_BAD_STATE, &txn->op);
        txn = list_remove_head_type(&dev->deferred_list, ramdisk_txn_t, node);

        if (txn == nullptr) {
            fbl::AutoLock lock(&dev->lock_);
            txn = list_remove_head_type(&dev->txn_list, ramdisk_txn_t, node);
        }
    }
    return 0;
}

uint64_t sizebytes(ramdisk_device_t* rdev) {
    return rdev->block_size * rdev->block_count;
}

// implement device protocol:

void ramdisk_unbind(void* ctx) {
    ramdisk_device_t* ramdev = static_cast<ramdisk_device_t*>(ctx);
    {
        fbl::AutoLock lock(&ramdev->lock_);
        ramdev->dead = true;
    }
    sync_completion_signal(&ramdev->signal);
    device_remove(ramdev->zxdev);
}

zx_status_t ramdisk_ioctl(void* ctx, uint32_t op, const void* cmd, size_t cmd_len,
                                 void* reply, size_t max, size_t* out_actual) {
    ramdisk_device_t* ramdev = static_cast<ramdisk_device_t*>(ctx);

    switch (op) {
    case IOCTL_RAMDISK_UNLINK: {
        ramdisk_unbind(ramdev);
        return ZX_OK;
    }
    case IOCTL_RAMDISK_SET_FLAGS: {
        if (cmd_len < sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        const uint32_t flags = *static_cast<const uint32_t*>(cmd);
        fbl::AutoLock lock(&ramdev->lock_);
        ramdev->flags = flags;
        return ZX_OK;
    }
    case IOCTL_RAMDISK_WAKE_UP: {
        // Reset state and transaction counts
        fbl::AutoLock lock(&ramdev->lock_);
        ramdev->asleep = false;
        memset(&ramdev->block_counts, 0, sizeof(ramdev->block_counts));
        ramdev->pre_sleep_write_block_count = 0;
        sync_completion_signal(&ramdev->signal);
        return ZX_OK;
    }
    case IOCTL_RAMDISK_SLEEP_AFTER: {
        if (cmd_len < sizeof(uint64_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        const uint64_t block_count = *static_cast<const uint64_t*>(cmd);
        fbl::AutoLock lock(&ramdev->lock_);
        ramdev->asleep = false;
        memset(&ramdev->block_counts, 0, sizeof(ramdev->block_counts));
        ramdev->pre_sleep_write_block_count = block_count;

        if (block_count == 0) {
            ramdev->asleep = true;
        }
        return ZX_OK;
    }
    case IOCTL_RAMDISK_GET_BLK_COUNTS: {
        if (max < sizeof(ramdisk_blk_counts_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        fbl::AutoLock lock(&ramdev->lock_);
        memcpy(reply, &ramdev->block_counts, sizeof(ramdisk_blk_counts_t));
        *out_actual = sizeof(ramdisk_blk_counts_t);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void ramdisk_queue(void* ctx, block_op_t* bop, block_impl_queue_callback completion_cb,
                          void* cookie) {
    ramdisk_device_t* ramdev = static_cast<ramdisk_device_t*>(ctx);
    ramdisk_txn_t* txn = containerof(bop, ramdisk_txn_t, op);
    bool dead;
    bool read = false;

    switch ((txn->op.command &= BLOCK_OP_MASK)) {
    case BLOCK_OP_READ:
        read = true;
        __FALLTHROUGH;
    case BLOCK_OP_WRITE:
        if ((txn->op.rw.offset_dev >= ramdev->block_count) ||
            ((ramdev->block_count - txn->op.rw.offset_dev) < txn->op.rw.length)) {
            completion_cb(cookie, ZX_ERR_OUT_OF_RANGE, bop);
            return;
        }

        {
            fbl::AutoLock lock(&ramdev->lock_);
            if (!(dead = ramdev->dead)) {
                if (!read) {
                    ramdev->block_counts.received += txn->op.rw.length;
                }
                txn->completion_cb = completion_cb;
                txn->cookie = cookie;
                list_add_tail(&ramdev->txn_list, &txn->node);
            }
        }

        if (dead) {
            completion_cb(cookie, ZX_ERR_BAD_STATE, bop);
        } else {
            sync_completion_signal(&ramdev->signal);
        }
        break;
    case BLOCK_OP_FLUSH:
        completion_cb(cookie, ZX_OK, bop);
        break;
    default:
        completion_cb(cookie, ZX_ERR_NOT_SUPPORTED, bop);
        break;
    }
}

void ramdisk_query(void* ctx, block_info_t* info, size_t* bopsz) {
    ramdisk_device_t* ramdev = static_cast<ramdisk_device_t*>(ctx);
    memset(info, 0, sizeof(*info));
    info->block_size = static_cast<uint32_t>(ramdev->block_size);
    info->block_count = ramdev->block_count;
    // Arbitrarily set, but matches the SATA driver for testing
    info->max_transfer_size = kMaxTransferSize;
    fbl::AutoLock lock(&ramdev->lock_);
    info->flags = ramdev->flags;
    *bopsz = sizeof(ramdisk_txn_t);
}

zx_off_t ramdisk_getsize(void* ctx) {
    return sizebytes(static_cast<ramdisk_device_t*>(ctx));
}

void ramdisk_release(void* ctx) {
    ramdisk_device_t* ramdev = static_cast<ramdisk_device_t*>(ctx);

    // Wake up the worker thread, in case it is sleeping
    sync_completion_signal(&ramdev->signal);

    thrd_join(ramdev->worker, nullptr);
    delete ramdev;
}

static block_impl_protocol_ops_t block_ops = {
    .query = ramdisk_query,
    .queue = ramdisk_queue,
};

static_assert(ZBI_PARTITION_GUID_LEN == GUID_LENGTH, "GUID length mismatch");

zx_status_t ramdisk_get_guid(void* ctx, guidtype_t guidtype, guid_t* out_guid) {
    if (guidtype != GUIDTYPE_TYPE) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    ramdisk_device_t* device = static_cast<ramdisk_device_t*>(ctx);
    memcpy(out_guid, device->type_guid, ZBI_PARTITION_GUID_LEN);
    return ZX_OK;
}

static_assert(ZBI_PARTITION_NAME_LEN <= MAX_PARTITION_NAME_LENGTH, "Name length mismatch");

zx_status_t ramdisk_get_name(void* ctx, char* out_name, size_t capacity) {
    if (capacity < ZBI_PARTITION_NAME_LEN) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    ramdisk_device_t* device = static_cast<ramdisk_device_t*>(ctx);
    strlcpy(out_name, device->name, ZBI_PARTITION_NAME_LEN);
    return ZX_OK;
}

static block_partition_protocol_ops_t partition_ops = {
    .get_guid = ramdisk_get_guid,
    .get_name = ramdisk_get_name,
};

zx_status_t ramdisk_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    ramdisk_device_t* device = static_cast<ramdisk_device_t*>(ctx);
    switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL: {
        block_impl_protocol_t* protocol = static_cast<block_impl_protocol_t*>(out);
        protocol->ctx = device;
        protocol->ops = &block_ops;
        return ZX_OK;
    }
    case ZX_PROTOCOL_BLOCK_PARTITION: {
        block_partition_protocol_t* protocol = static_cast<block_partition_protocol_t*>(out);
        protocol->ctx = device;
        protocol->ops = &partition_ops;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static zx_protocol_device_t ramdisk_instance_proto = []() {
    zx_protocol_device_t protocol = {};
    protocol.version = DEVICE_OPS_VERSION;
    protocol.get_protocol = ramdisk_get_protocol;
    protocol.unbind = ramdisk_unbind;
    protocol.release = ramdisk_release;
    protocol.get_size = ramdisk_getsize;
    protocol.ioctl = ramdisk_ioctl;
    return protocol;
}();

// implement device protocol:

static uint64_t ramdisk_count = 0;

constexpr size_t kMaxRamdiskNameLength = 32;

zx_status_t ramctl_config(ramctl_device_t* ramctl, zx::vmo vmo,
                          uint64_t block_size, uint64_t block_count,
                          uint8_t* type_guid, void* reply, size_t max,
                          size_t* out_actual) {
    if (max < kMaxRamdiskNameLength) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto ramdev = std::make_unique<ramdisk_device_t>();
    ramdev->block_size = block_size;
    ramdev->block_count = block_count;
    if (type_guid) {
        memcpy(ramdev->type_guid, type_guid, ZBI_PARTITION_GUID_LEN);
    } else {
        memset(ramdev->type_guid, 0, ZBI_PARTITION_GUID_LEN);
    }
    snprintf(ramdev->name, sizeof(ramdev->name),
             "ramdisk-%" PRIu64, ramdisk_count++);

    zx_status_t status = ramdev->mapping.Map(std::move(vmo), sizebytes(ramdev.get()));
    if (status != ZX_OK) {
        return status;
    }
    list_initialize(&ramdev->txn_list);
    list_initialize(&ramdev->deferred_list);
    if (thrd_create(&ramdev->worker, worker_thread, ramdev.get()) != thrd_success) {
        return ZX_ERR_NO_MEMORY;
    }

    device_add_args_t args;
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = ramdev->name;
    args.ctx = ramdev.get();
    args.ops = &ramdisk_instance_proto;
    args.props = nullptr;
    args.prop_count = 0;
    args.proto_id = ZX_PROTOCOL_BLOCK_IMPL;
    args.proto_ops = &block_ops;
    args.proxy_args = nullptr;
    args.flags = 0;

    char* name = static_cast<char*>(reply);
    strcpy(name, ramdev->name);
    size_t namelen = strlen(name);

    if ((status = device_add(ramctl->zxdev, &args, &ramdev->zxdev)) != ZX_OK) {
        ramdisk_release(ramdev.release());
        return status;
    }
    __UNUSED auto ptr = ramdev.release();
    *out_actual = namelen;
    return ZX_OK;
}

zx_status_t ramctl_ioctl(void* ctx, uint32_t op, const void* cmd,
                                size_t cmdlen, void* reply, size_t max, size_t* out_actual) {
    ramctl_device_t* ramctl = static_cast<ramctl_device_t*>(ctx);

    switch (op) {
    case IOCTL_RAMDISK_CONFIG: {
        if (cmdlen != sizeof(ramdisk_ioctl_config_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        ramdisk_ioctl_config_t* config = (ramdisk_ioctl_config_t*)cmd;
        zx::vmo vmo;
        zx_status_t status = zx::vmo::create( config->blk_size * config->blk_count, 0, &vmo);
        if (status == ZX_OK) {
            status = ramctl_config(ramctl, std::move(vmo),
                                   config->blk_size, config->blk_count,
                                   config->type_guid,
                                   reply, max, out_actual);
        }
        return status;
    }
    case IOCTL_RAMDISK_CONFIG_VMO: {
        if (cmdlen != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx::vmo vmo = zx::vmo(*reinterpret_cast<const zx_handle_t*>(cmd));

        // Ensure this is the last handle to this VMO; otherwise, the size
        // may change from underneath us.
        zx_info_handle_count_t info;
        zx_status_t status = vmo.get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr,
                                          nullptr);
        if (status != ZX_OK || info.handle_count != 1) {
            return ZX_ERR_INVALID_ARGS;
        }

        uint64_t vmo_size;
        status = vmo.get_size(&vmo_size);
        if (status != ZX_OK) {
            return status;
        }

        return ramctl_config(ramctl, std::move(vmo),
                             PAGE_SIZE, (vmo_size + PAGE_SIZE - 1) / PAGE_SIZE,
                             nullptr, reply, max, out_actual);
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_protocol_device_t ramdisk_ctl_proto = []() {
    zx_protocol_device_t protocol = {};
    protocol.version = DEVICE_OPS_VERSION;
    protocol.ioctl = ramctl_ioctl;
    return protocol;
}();

zx_status_t ramdisk_driver_bind(void* ctx, zx_device_t* parent) {
    auto ramctl = std::make_unique<ramctl_device_t>();

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "ramctl";
    args.ops = &ramdisk_ctl_proto;
    args.ctx = ramctl.get();

    zx_status_t status = device_add(parent, &args, &ramctl->zxdev);
    if (status != ZX_OK) {
        return status;
    }

    __UNUSED auto ptr = ramctl.release();
    return ZX_OK;
}

static zx_driver_ops_t ramdisk_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = ramdisk_driver_bind;
    return ops;
}();

} // namespace

ZIRCON_DRIVER_BEGIN(ramdisk, ramdisk_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(ramdisk)
