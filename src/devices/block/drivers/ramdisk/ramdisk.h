// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_RAMDISK_RAMDISK_H_
#define SRC_DEVICES_BLOCK_DRIVERS_RAMDISK_RAMDISK_H_

#include <fuchsia/hardware/block/cpp/banjo.h>
#include <fuchsia/hardware/block/partition/cpp/banjo.h>
#include <fuchsia/hardware/ramdisk/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/operation/block.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/vmo.h>
#include <threads.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

#include <memory>
#include <vector>

#include <ddktl/device.h>
#include <fbl/mutex.h>

namespace ramdisk {

class Ramdisk;
using RamdiskDeviceType =
    ddk::Device<Ramdisk, ddk::GetProtocolable, ddk::GetSizable, ddk::Unbindable, ddk::MessageableOld>;

class Ramdisk : public RamdiskDeviceType,
                public ddk::BlockImplProtocol<Ramdisk, ddk::base_protocol>,
                public ddk::BlockPartitionProtocol<Ramdisk> {
 public:
  Ramdisk(const Ramdisk&) = delete;
  Ramdisk& operator=(const Ramdisk&) = delete;

  static zx_status_t Create(zx_device_t* parent, zx::vmo vmo, uint64_t block_size,
                            uint64_t block_count, const uint8_t* type_guid,
                            std::unique_ptr<Ramdisk>* out);

  const char* Name() const { return &name_[0]; }

  // Device Protocol
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
  zx_off_t DdkGetSize();
  void DdkUnbind(ddk::UnbindTxn txn);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease();

  // Block Protocol
  void BlockImplQuery(block_info_t* info_out, size_t* block_op_size_out);
  void BlockImplQueue(block_op_t* txn, block_impl_queue_callback completion_cb, void* cookie);

  // FIDL interface Ramdisk
  zx_status_t FidlSetFlags(uint32_t flags, fidl_txn_t* txn);
  zx_status_t FidlWake(fidl_txn_t* txn);
  zx_status_t FidlSleepAfter(uint64_t count, fidl_txn_t* txn);
  zx_status_t FidlGetBlockCounts(fidl_txn_t* txn);
  zx_status_t FidlGrow(uint64_t required_size, fidl_txn_t* txn);

  // Partition Protocol
  zx_status_t BlockPartitionGetGuid(guidtype_t guid_type, guid_t* out_guid);
  zx_status_t BlockPartitionGetName(char* out_name, size_t capacity);

 private:
  Ramdisk(zx_device_t* parent, uint64_t block_size, uint64_t block_count, const uint8_t* type_guid,
          fzl::ResizeableVmoMapper mapping);

  // Processes requests made to the ramdisk until it is unbound.
  void ProcessRequests();

  static const fuchsia_hardware_ramdisk_Ramdisk_ops* Ops() {
    using Binder = fidl::Binder<Ramdisk>;

    static const fuchsia_hardware_ramdisk_Ramdisk_ops kOps = {
        .SetFlags = Binder::BindMember<&Ramdisk::FidlSetFlags>,
        .Wake = Binder::BindMember<&Ramdisk::FidlWake>,
        .SleepAfter = Binder::BindMember<&Ramdisk::FidlSleepAfter>,
        .GetBlockCounts = Binder::BindMember<&Ramdisk::FidlGetBlockCounts>,
        .Grow = Binder::BindMember<&Ramdisk::FidlGrow>,
    };
    return &kOps;
  }

  static int WorkerThunk(void* arg) {
    Ramdisk* dev = reinterpret_cast<Ramdisk*>(arg);
    dev->ProcessRequests();
    return 0;
  }

  uint64_t block_size_;
  uint64_t block_count_;
  uint8_t type_guid_[ZBI_PARTITION_GUID_LEN];
  fzl::ResizeableVmoMapper mapping_;

  // |signal| identifies when the worker thread should stop sleeping.
  // This may occur when the device:
  // - Is unbound,
  // - Received a message on a queue,
  // - Has |asleep| set to false.
  sync_completion_t signal_;

  // This is threadsafe.
  block::BorrowedOperationQueue<> txn_list_;

  // Guards fields of the ramdisk which may be accessed concurrently
  // from a background worker thread.
  fbl::Mutex lock_;

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
  //
  // This functionality is used by the journaling tests.  Maybe to be used in other tests.  It gives
  // precise control over what data is preserve.  Do not use outside of tests.
  bool asleep_ TA_GUARDED(lock_) = false;

  // The number of blocks-to-be-written that should be processed.
  // When this reaches zero, the ramdisk will set |asleep| to true.
  //
  // See |asleep_| comment above for reasoning.
  uint64_t pre_sleep_write_block_count_ TA_GUARDED(lock_) = 0;
  fuchsia_hardware_ramdisk_BlockWriteCounts block_counts_ TA_GUARDED(lock_){};

  thrd_t worker_ = {};
  char name_[ZBI_PARTITION_NAME_LEN];

  std::vector<uint64_t> blocks_written_since_last_flush_ TA_GUARDED(lock_);
};

}  // namespace ramdisk

#endif  // SRC_DEVICES_BLOCK_DRIVERS_RAMDISK_RAMDISK_H_
