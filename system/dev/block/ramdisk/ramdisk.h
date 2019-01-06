// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/block.h>
#include <ddktl/protocol/block/partition.h>
#include <fbl/mutex.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/vmo.h>
#include <zircon/device/block.h>
#include <zircon/device/ramdisk.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

#include "transaction.h"

namespace ramdisk {

class Ramdisk;
using RamdiskDeviceType = ddk::Device<Ramdisk,
                                      ddk::GetProtocolable,
                                      ddk::GetSizable,
                                      ddk::Unbindable,
                                      ddk::Ioctlable>;

class Ramdisk : public RamdiskDeviceType,
                public ddk::BlockImplProtocol<Ramdisk, ddk::base_protocol>,
                public ddk::BlockPartitionProtocol<Ramdisk> {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Ramdisk);

    static zx_status_t Create(zx_device_t* parent, zx::vmo vmo, uint64_t block_size,
                              uint64_t block_count, uint8_t* type_guid,
                              std::unique_ptr<Ramdisk>* out);

    const char* Name() const {
        return &name_[0];
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
            uint8_t* type_guid, fzl::OwnedVmoMapper mapping);

    // Processes requests made to the ramdisk until it is unbound.
    void ProcessRequests();

    static int WorkerThunk(void* arg) {
        Ramdisk* dev = reinterpret_cast<Ramdisk*>(arg);
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
} // namespace ramdisk
