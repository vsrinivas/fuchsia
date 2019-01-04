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
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <ddktl/protocol/block/partition.h>
#include <fbl/auto_lock.h>
#include <fbl/atomic.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>
#include <zircon/device/block.h>
#include <zircon/device/ramdisk.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include "transaction.h"

namespace ramdisk {
namespace {

constexpr uint64_t kMaxTransferSize = 1LLU << 19;

class RamdiskController;
using RamdiskControllerDeviceType = ddk::Device<RamdiskController, ddk::Ioctlable>;

class RamdiskController : public RamdiskControllerDeviceType {
public:
    RamdiskController(zx_device_t* parent) : RamdiskControllerDeviceType(parent) {}

    // Device Protocol
    zx_status_t DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen, void* reply, size_t max,
                         size_t* out_actual);
    void DdkRelease() {
        delete this;
    }

private:
    // Other methods
    zx_status_t ConfigureDevice(zx::vmo vmo, uint64_t block_size, uint64_t block_count,
                                uint8_t* type_guid, void* reply, size_t max, size_t* out_actual);
};

class Ramdisk;
using RamdiskDeviceType = ddk::Device<Ramdisk,
                                      ddk::GetProtocolable,
                                      ddk::GetSizable,
                                      ddk::Unbindable,
                                      ddk::Ioctlable>;

static fbl::atomic<uint64_t> ramdisk_count = 0;

class Ramdisk : public RamdiskDeviceType,
                public ddk::BlockImplProtocol<Ramdisk, ddk::base_protocol>,
                public ddk::BlockPartitionProtocol<Ramdisk> {
public:
    static zx_status_t Create(zx_device_t* parent, zx::vmo vmo, uint64_t block_size,
                              uint64_t block_count, uint8_t* type_guid,
                              std::unique_ptr<Ramdisk>* out) {
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
    const char* Name() const {
        return name_;
    }

    // Device Protocol
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
    zx_off_t DdkGetSize();
    void DdkUnbind();
    zx_status_t DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen,
                         void* reply, size_t max, size_t* out_actual);
    void DdkRelease();

    // Block Protocol
    void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);
    void BlockImplQueue(block_op_t* txn, block_impl_queue_callback completion_cb, void* cookie);

    // Partition Protocol
    zx_status_t BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid);
    zx_status_t BlockPartitionGetName(char* out_name, size_t capacity);

private:
    Ramdisk(zx_device_t* parent, uint64_t block_size, uint64_t block_count,
            uint8_t* type_guid, fzl::OwnedVmoMapper mapping)
        : RamdiskDeviceType(parent),
          block_size_(block_size),
          block_count_(block_count),
          mapping_(std::move(mapping)) {
        if (type_guid) {
            memcpy(type_guid_, type_guid, ZBI_PARTITION_GUID_LEN);
        } else {
            memset(type_guid_, 0, ZBI_PARTITION_GUID_LEN);
        }
        snprintf(name_, sizeof(name_), "ramdisk-%" PRIu64, ramdisk_count.fetch_add(1));
    }

    // Processes requests made to the ramdisk until it is unbound.
    void ProcessRequests();

    static int WorkerThunk(void* arg) {
        Ramdisk* dev = static_cast<Ramdisk*>(arg);
        dev->ProcessRequests();
        return 0;
    };

    uint64_t block_size_;
    uint64_t block_count_;
    uint8_t type_guid_[ZBI_PARTITION_GUID_LEN];
    fzl::OwnedVmoMapper mapping_;

    // |signal| identifies when the worker thread should stop sleeping.
    // This may occur when the device:
    // - Is unbound,
    // - Received a message on a queue,
    // - Has |asleep| set to false.
    sync_completion_t signal_;

    // Guards fields of the ramdisk which may be accessed concurrently
    // from a background worker thread.
    fbl::Mutex lock_;
    TransactionList txn_list_ TA_GUARDED(lock_);

    // Identifies if the device has been unbound.
    bool dead_ TA_GUARDED(lock_) = false;

    // Flags modified by RAMDISK_SET_FLAGS.
    //
    // Supported flags:
    // - RAMDISK_FLAG_RESUME_ON_WAKE: This flag identifies if requests which are
    // sent to the ramdisk while it is considered "alseep" should be processed
    // when the ramdisk wakes up. This is implemented by utilizing a "deferred
    // list" of requests, which are immediately re-issued on wakeup.
    uint32_t flags_ TA_GUARDED(lock_) = 0;

    // True if the ramdisk is "sleeping", and deferring all upcoming requests,
    // or dropping them if |RAMDISK_FLAG_RESUME_ON_WAKE| is not set.
    bool asleep_ TA_GUARDED(lock_) = false;
    // The number of blocks-to-be-written that should be processed.
    // When this reaches zero, the ramdisk will set |asleep| to true.
    uint64_t pre_sleep_write_block_count_ TA_GUARDED(lock_) = 0;
    ramdisk_blk_counts_t block_counts_ TA_GUARDED(lock_) {};

    thrd_t worker_ = {};
    char name_[ZBI_PARTITION_NAME_LEN];
};

// The worker thread processes messages from iotxns in the background
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

// implement device protocol:

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

zx_off_t Ramdisk::DdkGetSize() {
    return block_size_ * block_count_;
}

void Ramdisk::DdkRelease() {
    // Wake up the worker thread, in case it is sleeping
    sync_completion_signal(&signal_);

    thrd_join(worker_, nullptr);
    delete this;
}

static_assert(ZBI_PARTITION_GUID_LEN == GUID_LENGTH, "GUID length mismatch");

zx_status_t Ramdisk::BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid) {
    if (guid_type != GUIDTYPE_TYPE) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    memcpy(out_guid, type_guid_, ZBI_PARTITION_GUID_LEN);
    return ZX_OK;
}

static_assert(ZBI_PARTITION_NAME_LEN <= MAX_PARTITION_NAME_LENGTH, "Name length mismatch");

zx_status_t Ramdisk::BlockPartitionGetName(char* out_name, size_t capacity) {
    if (capacity < ZBI_PARTITION_NAME_LEN) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    strlcpy(out_name, name_, ZBI_PARTITION_NAME_LEN);
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

constexpr size_t kMaxRamdiskNameLength = 32;

zx_status_t RamdiskController::ConfigureDevice(zx::vmo vmo, uint64_t block_size,
                                               uint64_t block_count, uint8_t* type_guid,
                                               void* reply, size_t max, size_t* out_actual) {
    if (max < kMaxRamdiskNameLength) {
        return ZX_ERR_INVALID_ARGS;
    }

    std::unique_ptr<Ramdisk> ramdev;
    zx_status_t status = Ramdisk::Create(zxdev(), std::move(vmo), block_size, block_count,
                                         type_guid, &ramdev);
    if (status != ZX_OK) {
        return status;
    }

    char* name = static_cast<char*>(reply);
    size_t namelen = strlcpy(name, ramdev->Name(), max);

    if ((status = ramdev->DdkAdd(ramdev->Name()) != ZX_OK)) {
        ramdev.release()->DdkRelease();
        return status;
    }
    __UNUSED auto ptr = ramdev.release();
    *out_actual = namelen;
    return ZX_OK;
}

zx_status_t RamdiskController::DdkIoctl(uint32_t op, const void* cmd, size_t cmdlen, void* reply,
                                        size_t max, size_t* out_actual) {
    switch (op) {
    case IOCTL_RAMDISK_CONFIG: {
        if (cmdlen != sizeof(ramdisk_ioctl_config_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        ramdisk_ioctl_config_t* config = (ramdisk_ioctl_config_t*)cmd;
        zx::vmo vmo;
        zx_status_t status = zx::vmo::create( config->blk_size * config->blk_count, 0, &vmo);
        if (status == ZX_OK) {
            status = ConfigureDevice(std::move(vmo), config->blk_size, config->blk_count,
                                     config->type_guid, reply, max, out_actual);
        }
        return status;
    }
    case IOCTL_RAMDISK_CONFIG_VMO: {
        if (cmdlen != sizeof(zx_handle_t)) {
            return ZX_ERR_INVALID_ARGS;
        }
        zx::vmo vmo = zx::vmo(*static_cast<const zx_handle_t*>(cmd));

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

        return ConfigureDevice(std::move(vmo), PAGE_SIZE, (vmo_size + PAGE_SIZE - 1) / PAGE_SIZE,
                               nullptr, reply, max, out_actual);
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t RamdiskDriverBind(void* ctx, zx_device_t* parent) {
    auto ramctl = std::make_unique<RamdiskController>(parent);

    zx_status_t status = ramctl->DdkAdd("ramctl");
    if (status != ZX_OK) {
        return status;
    }

    // RamdiskController owned by the DDK after being added successfully.
    __UNUSED auto ptr = ramctl.release();
    return ZX_OK;
}

static zx_driver_ops_t ramdisk_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = RamdiskDriverBind;
    return ops;
}();

} // namespace
} // namespace ramdisk

ZIRCON_DRIVER_BEGIN(ramdisk, ramdisk::ramdisk_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT),
ZIRCON_DRIVER_END(ramdisk)
