// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/cpp/banjo.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <fuchsia/hardware/block/partition/cpp/banjo.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <fuchsia/hardware/block/volume/cpp/banjo.h>
#include <inttypes.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/metadata.h>
#include <lib/fidl-utils/bind.h>
#include <lib/operation/block.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <zircon/boot/image.h>
#include <zircon/process.h>

#include <algorithm>
#include <limits>
#include <list>
#include <new>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <storage-metrics/block-metrics.h>

#include "src/devices/block/drivers/core/block-core-bind.h"
#include "src/devices/block/drivers/core/manager.h"

#ifndef SRC_DEVICES_BLOCK_DRIVERS_CORE_BLOCK_DEVICE_H_
#define SRC_DEVICES_BLOCK_DRIVERS_CORE_BLOCK_DEVICE_H_

// To maintian stats related to time taken by a command or its success/failure, we need to
// intercept command completion with a callback routine. This might introduce memory
// overhead.
// TODO(auradkar): We should be able to turn on/off stats either at compile-time or load-time.
struct StatsCookie {
  zx::ticks start_tick;
};

class BlockDevice;
using BlockDeviceType = ddk::Device<BlockDevice, ddk::GetProtocolable, ddk::MessageableManual,
                                    ddk::Readable, ddk::Writable, ddk::GetSizable>;

class BlockDevice : public BlockDeviceType,
                    public ddk::BlockProtocol<BlockDevice, ddk::base_protocol> {
 public:
  explicit BlockDevice(zx_device_t* parent)
      : BlockDeviceType(parent),
        parent_protocol_(parent),
        parent_partition_protocol_(parent),
        parent_volume_protocol_(parent) {
    block_protocol_t self{&block_protocol_ops_, this};
    self_protocol_ = ddk::BlockProtocolClient(&self);
  }

  static zx_status_t Bind(void* ctx, zx_device_t* dev);

  constexpr size_t OpSize() const {
    ZX_DEBUG_ASSERT(parent_op_size_ > 0);
    return block::BorrowedOperation<StatsCookie>::OperationSize(parent_op_size_);
  }

  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_protocol);
  void DdkMessage(fidl::IncomingMessage&& msg, DdkTransaction& txn);
  zx_status_t DdkRead(void* buf, size_t buf_len, zx_off_t off, size_t* actual);
  zx_status_t DdkWrite(const void* buf, size_t buf_len, zx_off_t off, size_t* actual);
  zx_off_t DdkGetSize();

  void BlockQuery(block_info_t* block_info, size_t* op_size);
  void BlockQueue(block_op_t* op, block_impl_queue_callback completion_cb, void* cookie);
  zx_status_t GetStats(bool clear, block_stats_t* out);
  void UpdateStats(bool success, zx::ticks start_tick, block_op_t* op);

  static const fuchsia_hardware_block_Block_ops* BlockOps() {
    using Binder = fidl::Binder<BlockDevice>;
    static const fuchsia_hardware_block_Block_ops kOps = {
        .GetInfo = Binder::BindMember<&BlockDevice::FidlBlockGetInfo>,
        .GetStats = Binder::BindMember<&BlockDevice::FidlBlockGetStats>,
        .GetFifo = Binder::BindMember<&BlockDevice::FidlBlockGetFifo>,
        .AttachVmo = Binder::BindMember<&BlockDevice::FidlBlockAttachVmo>,
        .CloseFifo = Binder::BindMember<&BlockDevice::FidlBlockCloseFifo>,
        .RebindDevice = Binder::BindMember<&BlockDevice::FidlBlockRebindDevice>,
        .ReadBlocks = Binder::BindMember<&BlockDevice::FidlReadBlocks>,
        .WriteBlocks = Binder::BindMember<&BlockDevice::FidlWriteBlocks>,
    };
    return &kOps;
  }

 private:
  zx_status_t DoIo(zx_handle_t vmo, size_t buf_len, zx_off_t off, zx_off_t vmo_off, bool write);

  zx_status_t FidlBlockGetInfo(fidl_txn_t* txn);
  zx_status_t FidlBlockGetStats(bool clear, fidl_txn_t* txn);
  zx_status_t FidlBlockGetFifo(fidl_txn_t* txn);
  zx_status_t FidlBlockAttachVmo(zx_handle_t vmo, fidl_txn_t* txn);
  zx_status_t FidlBlockCloseFifo(fidl_txn_t* txn);
  zx_status_t FidlBlockRebindDevice(fidl_txn_t* txn);
  zx_status_t FidlReadBlocks(zx_handle_t vmo, uint64_t length, uint64_t dev_offset,
                             uint64_t vmo_offset, fidl_txn_t* txn);
  zx_status_t FidlWriteBlocks(zx_handle_t vmo, uint64_t length, uint64_t dev_offset,
                              uint64_t vmo_offset, fidl_txn_t* txn);
  zx_status_t FidlPartitionGetTypeGuid(fidl_txn_t* txn);
  zx_status_t FidlPartitionGetInstanceGuid(fidl_txn_t* txn);
  zx_status_t FidlPartitionGetName(fidl_txn_t* txn);
  zx_status_t FidlVolumeGetVolumeInfo(fidl_txn_t* txn);
  zx_status_t FidlVolumeQuerySlices(const uint64_t* start_slices_data, size_t start_slices_count,
                                    fidl_txn_t* txn);
  zx_status_t FidlVolumeExtend(uint64_t start_slice, uint64_t slice_count, fidl_txn_t* txn);
  zx_status_t FidlVolumeShrink(uint64_t start_slice, uint64_t slice_count, fidl_txn_t* txn);
  zx_status_t FidlVolumeDestroy(fidl_txn_t* txn);

  // Converts BlockDeviceMetrics to block_stats_t
  void ConvertToBlockStats(block_stats_t* out) __TA_REQUIRES(stat_lock_);

  // Completion callback that expects StatsCookie as |cookie| and calls upper
  // layer completion cookie.
  static void UpdateStatsAndCallCompletion(void* cookie, zx_status_t status, block_op_t* op);

  static const fuchsia_hardware_block_partition_Partition_ops* PartitionOps() {
    using Binder = fidl::Binder<BlockDevice>;
    static const fuchsia_hardware_block_partition_Partition_ops kOps = {
        .GetInfo = Binder::BindMember<&BlockDevice::FidlBlockGetInfo>,
        .GetStats = Binder::BindMember<&BlockDevice::FidlBlockGetStats>,
        .GetFifo = Binder::BindMember<&BlockDevice::FidlBlockGetFifo>,
        .AttachVmo = Binder::BindMember<&BlockDevice::FidlBlockAttachVmo>,
        .CloseFifo = Binder::BindMember<&BlockDevice::FidlBlockCloseFifo>,
        .RebindDevice = Binder::BindMember<&BlockDevice::FidlBlockRebindDevice>,
        .ReadBlocks = Binder::BindMember<&BlockDevice::FidlReadBlocks>,
        .WriteBlocks = Binder::BindMember<&BlockDevice::FidlWriteBlocks>,
        .GetTypeGuid = Binder::BindMember<&BlockDevice::FidlPartitionGetTypeGuid>,
        .GetInstanceGuid = Binder::BindMember<&BlockDevice::FidlPartitionGetInstanceGuid>,
        .GetName = Binder::BindMember<&BlockDevice::FidlPartitionGetName>,
    };
    return &kOps;
  }

  static const fuchsia_hardware_block_volume_Volume_ops* VolumeOps() {
    using Binder = fidl::Binder<BlockDevice>;
    static const fuchsia_hardware_block_volume_Volume_ops kOps = {
        .GetInfo = Binder::BindMember<&BlockDevice::FidlBlockGetInfo>,
        .GetStats = Binder::BindMember<&BlockDevice::FidlBlockGetStats>,
        .GetFifo = Binder::BindMember<&BlockDevice::FidlBlockGetFifo>,
        .AttachVmo = Binder::BindMember<&BlockDevice::FidlBlockAttachVmo>,
        .CloseFifo = Binder::BindMember<&BlockDevice::FidlBlockCloseFifo>,
        .RebindDevice = Binder::BindMember<&BlockDevice::FidlBlockRebindDevice>,
        .ReadBlocks = Binder::BindMember<&BlockDevice::FidlReadBlocks>,
        .WriteBlocks = Binder::BindMember<&BlockDevice::FidlWriteBlocks>,
        .GetTypeGuid = Binder::BindMember<&BlockDevice::FidlPartitionGetTypeGuid>,
        .GetInstanceGuid = Binder::BindMember<&BlockDevice::FidlPartitionGetInstanceGuid>,
        .GetName = Binder::BindMember<&BlockDevice::FidlPartitionGetName>,
        .QuerySlices = Binder::BindMember<&BlockDevice::FidlVolumeQuerySlices>,
        .GetVolumeInfo = Binder::BindMember<&BlockDevice::FidlVolumeGetVolumeInfo>,
        .Extend = Binder::BindMember<&BlockDevice::FidlVolumeExtend>,
        .Shrink = Binder::BindMember<&BlockDevice::FidlVolumeShrink>,
        .Destroy = Binder::BindMember<&BlockDevice::FidlVolumeDestroy>,
    };
    return &kOps;
  }

  // The block protocol of the device we are binding against.
  ddk::BlockImplProtocolClient parent_protocol_;
  // An optional partition protocol, if supported by the parent device.
  ddk::BlockPartitionProtocolClient parent_partition_protocol_;
  // An optional volume protocol, if supported by the parent device.
  ddk::BlockVolumeProtocolClient parent_volume_protocol_;
  // The block protocol for ourselves, which redirects to the parent protocol,
  // but may also collect auxiliary information like statistics.
  ddk::BlockProtocolClient self_protocol_;
  block_info_t info_ = {};

  // parent device's op size
  size_t parent_op_size_ = 0;

  // True if we have metadata for a ZBI partition map.
  bool has_bootpart_ = false;

  // Manages the background FIFO server.
  Manager manager_;

  fbl::Mutex io_lock_;
  zx::vmo io_vmo_ TA_GUARDED(io_lock_);
  zx_status_t io_status_ = ZX_OK;
  sync_completion_t io_signal_;
  std::unique_ptr<uint8_t[]> io_op_;

  fbl::Mutex stat_lock_;
  // TODO(kmerrick) have this start as false and create IOCTL to toggle it.
  bool enable_stats_ TA_GUARDED(stat_lock_) = true;
  storage_metrics::BlockDeviceMetrics stats_ TA_GUARDED(stat_lock_) = {};

  // To maintain stats related to time taken by a command or its success/failure, we need to
  // intercept command completion with a callback routine. This might introduce cpu
  // overhead.
  // TODO(auradkar): We should be able to turn on/off stats at run-time.
  //                 Create fidl interface to control how stats are maintained.
  bool completion_status_stats_ = true;
};

#endif  // SRC_DEVICES_BLOCK_DRIVERS_CORE_BLOCK_DEVICE_H_
