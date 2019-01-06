// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <memory>

#include <inttypes.h>
#include <threads.h>

#include <ddk/driver.h>
#include <fbl/atomic.h>
#include <fbl/auto_lock.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/device/ramdisk.h>
#include <zircon/types.h>

#include "ramdisk.h"
#include "transaction.h"

namespace ramdisk {
namespace {

constexpr uint64_t kMaxTransferSize = 1LLU << 19;

static fbl::atomic<uint64_t> g_ramdisk_count = 0;

} // namespace

Ramdisk::Ramdisk(zx_device_t* parent, uint64_t block_size, uint64_t block_count,
                 uint8_t* type_guid, fzl::OwnedVmoMapper mapping)
    : RamdiskDeviceType(parent), block_size_(block_size), block_count_(block_count),
      mapping_(std::move(mapping)) {
    if (type_guid) {
        memcpy(type_guid_, type_guid, ZBI_PARTITION_GUID_LEN);
    } else {
        memset(type_guid_, 0, ZBI_PARTITION_GUID_LEN);
    }
    snprintf(name_, sizeof(name_), "ramdisk-%" PRIu64, g_ramdisk_count.fetch_add(1));
}

zx_status_t Ramdisk::Create(zx_device_t* parent, zx::vmo vmo, uint64_t block_size,
                            uint64_t block_count, uint8_t* type_guid, std::unique_ptr<Ramdisk>* out)
{
    fzl::OwnedVmoMapper mapping;
    zx_status_t status = mapping.Map(std::move(vmo), block_size * block_count);
    if (status != ZX_OK) {
        return status;
    }

    auto ramdev = std::unique_ptr<Ramdisk>(
            new Ramdisk(parent, block_size, block_count, type_guid, std::move(mapping)));
    if (thrd_create(&ramdev->worker_, WorkerThunk, ramdev.get()) != thrd_success) {
        return ZX_ERR_NO_MEMORY;
    }

    *out = std::move(ramdev);
    return ZX_OK;
}

zx_status_t Ramdisk::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
    auto* proto = static_cast<ddk::AnyProtocol*>(out_protocol);
    proto->ctx = this;
    switch (proto_id) {
    case ZX_PROTOCOL_BLOCK_IMPL: {
        proto->ops = &block_impl_protocol_ops_;
        return ZX_OK;
    }
    case ZX_PROTOCOL_BLOCK_PARTITION: {
        proto->ops = &block_partition_protocol_ops_;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_off_t Ramdisk::DdkGetSize() {
    return block_size_ * block_count_;
}

void Ramdisk::DdkUnbind() {
    {
        fbl::AutoLock lock(&lock_);
        dead_ = true;
    }
    sync_completion_signal(&signal_);
    DdkRemove();
}

zx_status_t Ramdisk::DdkIoctl(uint32_t op, const void* cmd, size_t cmd_len,
                              void* reply, size_t max, size_t* out_actual) {
    switch (op) {
    case IOCTL_RAMDISK_UNLINK: {
        DdkUnbind();
        return ZX_OK;
    }
    case IOCTL_RAMDISK_SET_FLAGS: {
        if (cmd_len < sizeof(uint32_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        const uint32_t flags = *static_cast<const uint32_t*>(cmd);
        fbl::AutoLock lock(&lock_);
        flags_ = flags;
        return ZX_OK;
    }
    case IOCTL_RAMDISK_WAKE_UP: {
        // Reset state and transaction counts
        fbl::AutoLock lock(&lock_);
        asleep_ = false;
        memset(&block_counts_, 0, sizeof(block_counts_));
        pre_sleep_write_block_count_ = 0;
        sync_completion_signal(&signal_);
        return ZX_OK;
    }
    case IOCTL_RAMDISK_SLEEP_AFTER: {
        if (cmd_len < sizeof(uint64_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        const uint64_t block_count = *static_cast<const uint64_t*>(cmd);
        fbl::AutoLock lock(&lock_);
        asleep_ = false;
        memset(&block_counts_, 0, sizeof(block_counts_));
        pre_sleep_write_block_count_ = block_count;

        if (block_count == 0) {
            asleep_ = true;
        }
        return ZX_OK;
    }
    case IOCTL_RAMDISK_GET_BLK_COUNTS: {
        if (max < sizeof(ramdisk_blk_counts_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        fbl::AutoLock lock(&lock_);
        memcpy(reply, &block_counts_, sizeof(ramdisk_blk_counts_t));
        *out_actual = sizeof(ramdisk_blk_counts_t);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void Ramdisk::DdkRelease() {
    // Wake up the worker thread, in case it is sleeping
    sync_completion_signal(&signal_);

    thrd_join(worker_, nullptr);
    delete this;
}

void Ramdisk::BlockImplQuery(block_info_t* info, size_t* bopsz) {
    memset(info, 0, sizeof(*info));
    info->block_size = static_cast<uint32_t>(block_size_);
    info->block_count = block_count_;
    // Arbitrarily set, but matches the SATA driver for testing
    info->max_transfer_size = kMaxTransferSize;
    fbl::AutoLock lock(&lock_);
    info->flags = flags_;
    *bopsz = sizeof(Transaction);
}

void Ramdisk::BlockImplQueue(block_op_t* bop, block_impl_queue_callback completion_cb,
                             void* cookie) {
    Transaction* txn = Transaction::InitFromOp(bop, completion_cb, cookie);
    bool dead;
    bool read = false;

    switch ((txn->op.command &= BLOCK_OP_MASK)) {
    case BLOCK_OP_READ:
        read = true;
        __FALLTHROUGH;
    case BLOCK_OP_WRITE:
        if ((txn->op.rw.offset_dev >= block_count_) ||
            ((block_count_ - txn->op.rw.offset_dev) < txn->op.rw.length)) {
            txn->Complete(ZX_ERR_OUT_OF_RANGE);
            return;
        }

        {
            fbl::AutoLock lock(&lock_);
            if (!(dead = dead_)) {
                if (!read) {
                    block_counts_.received += txn->op.rw.length;
                }
                txn_list_.push_back(txn);
            }
        }

        if (dead) {
            txn->Complete(ZX_ERR_BAD_STATE);
        } else {
            sync_completion_signal(&signal_);
        }
        break;
    case BLOCK_OP_FLUSH:
        txn->Complete(ZX_OK);
        break;
    default:
        txn->Complete(ZX_ERR_NOT_SUPPORTED);
        break;
    }
}

zx_status_t Ramdisk::BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid) {
    if (guid_type != GUIDTYPE_TYPE) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    static_assert(ZBI_PARTITION_GUID_LEN == GUID_LENGTH, "GUID length mismatch");
    memcpy(out_guid, type_guid_, ZBI_PARTITION_GUID_LEN);
    return ZX_OK;
}

zx_status_t Ramdisk::BlockPartitionGetName(char* out_name, size_t capacity) {
    if (capacity < ZBI_PARTITION_NAME_LEN) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    static_assert(ZBI_PARTITION_NAME_LEN <= MAX_PARTITION_NAME_LENGTH, "Name length mismatch");
    strlcpy(out_name, name_, ZBI_PARTITION_NAME_LEN);
    return ZX_OK;
}

void Ramdisk::ProcessRequests() {
    zx_status_t status = ZX_OK;
    Transaction* txn = nullptr;
    bool dead, asleep, defer;
    uint64_t blocks = 0;
    TransactionList deferred_list;

    for (;;) {
        for (;;) {
            {
                fbl::AutoLock lock(&lock_);
                txn = nullptr;
                dead = dead_;
                asleep = asleep_;
                defer = (flags_ & RAMDISK_FLAG_RESUME_ON_WAKE) != 0;
                blocks = pre_sleep_write_block_count_;

                if (!asleep) {
                    // If we are awake, try grabbing pending transactions from the deferred list.
                    txn = deferred_list.pop_front();
                }

                if (txn == nullptr) {
                    // If no transactions were available in the deferred list (or we are asleep),
                    // grab one from the regular txn_list.
                    txn = txn_list_.pop_front();
                }
            }

            if (dead) {
                goto goodbye;
            }

            if (txn == nullptr) {
                sync_completion_wait(&signal_, ZX_TIME_INFINITE);
            } else {
                sync_completion_reset(&signal_);
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

        size_t length = blocks * block_size_;
        size_t dev_offset = txn->op.rw.offset_dev * block_size_;
        size_t vmo_offset = txn->op.rw.offset_vmo * block_size_;
        void* addr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mapping_.start()) +
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
                deferred_list.push_back(txn);
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
                deferred_list.push_back(txn);
            }
        }

        if (txn->op.command == BLOCK_OP_WRITE) {
            {
                // Update the ramdisk block counts. Since we aren't failing read transactions,
                // only include write transaction counts.
                fbl::AutoLock lock(&lock_);
                // Increment the count based on the result of the last transaction.
                if (status == ZX_OK) {
                    block_counts_.successful += blocks;

                    if (blocks != txn_blocks && !defer) {
                        // If we are not deferring, then any excess blocks have failed.
                        block_counts_.failed += txn_blocks - blocks;
                        status = ZX_ERR_UNAVAILABLE;
                    }
                } else {
                    block_counts_.failed += txn_blocks;
                }

                // Put the ramdisk to sleep if we have reached the required # of blocks.
                if (pre_sleep_write_block_count_ > 0) {
                    pre_sleep_write_block_count_ -= blocks;
                    asleep_ = (pre_sleep_write_block_count_ == 0);
                }
            }

            if (defer && blocks != txn_blocks && status == ZX_OK) {
                // If we deferred partway through a transaction, hold off on returning the
                // result until the remainder of the transaction is completed.
                continue;
            }
        }

        txn->Complete(status);
    }

goodbye:
    while (txn != nullptr) {
        txn->Complete(ZX_ERR_BAD_STATE);
        txn = deferred_list.pop_front();

        if (txn == nullptr) {
            fbl::AutoLock lock(&lock_);
            txn = txn_list_.pop_front();
        }
    }
}

} // namespace ramdisk
