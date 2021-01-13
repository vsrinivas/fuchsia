// Copyright 2017 The Fuchsia Authors. All rights reserved.
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
#include <new>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <storage-metrics/block-metrics.h>

#include "src/devices/block/drivers/core/block-core-bind.h"
#include "src/devices/block/drivers/core/manager.h"

class BlockDevice;

namespace {

using storage_metrics::BlockDeviceMetrics;
using BlockDeviceType = ddk::Device<BlockDevice, ddk::GetProtocolable, ddk::Messageable,
                                    ddk::Unbindable, ddk::Readable, ddk::Writable, ddk::GetSizable>;

struct StatsCookie {
  zx::ticks start_tick;
};

// To maintian stats related to time taken by a command or its success/failure, we need to
// intercept command completion with a callback routine. This might introduce memory
// overhead.
// TODO(auradkar): We should be able to turn on/off stats either at compile-time or load-time.
using Transaction = block::BorrowedOperation<StatsCookie>;

}  // namespace

class BlockDevice : public BlockDeviceType,
                    public ddk::BlockProtocol<BlockDevice, ddk::base_protocol> {
 public:
  BlockDevice(zx_device_t* parent)
      : BlockDeviceType(parent),
        parent_protocol_(parent),
        parent_partition_protocol_(parent),
        parent_volume_protocol_(parent) {
    block_protocol_t self{&block_protocol_ops_, this};
    self_protocol_ = ddk::BlockProtocolClient(&self);
  }

  static zx_status_t Bind(void* ctx, zx_device_t* dev);

  constexpr size_t OpSize() {
    ZX_DEBUG_ASSERT(parent_op_size_ > 0);
    return Transaction::OperationSize(parent_op_size_);
  }

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_protocol);
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  zx_status_t DdkRead(void* buf, size_t buf_len, zx_off_t off, size_t* actual);
  zx_status_t DdkWrite(const void* buf, size_t buf_len, zx_off_t off, size_t* actual);
  zx_off_t DdkGetSize();

  void BlockQuery(block_info_t* block_info, size_t* op_size);
  void BlockQueue(block_op_t* op, block_impl_queue_callback completion_cb, void* cookie);
  zx_status_t GetStats(bool clear, block_stats_t* out);
  void UpdateStats(bool success, zx::ticks start_tick, block_op_t* op);

 private:
  zx_status_t DoIo(void* buf, size_t buf_len, zx_off_t off, bool write);

  zx_status_t FidlBlockGetInfo(fidl_txn_t* txn);
  zx_status_t FidlBlockGetStats(bool clear, fidl_txn_t* txn);
  zx_status_t FidlBlockGetFifo(fidl_txn_t* txn);
  zx_status_t FidlBlockAttachVmo(zx_handle_t vmo, fidl_txn_t* txn);
  zx_status_t FidlBlockCloseFifo(fidl_txn_t* txn);
  zx_status_t FidlBlockRebindDevice(fidl_txn_t* txn);
  zx_status_t FidlPartitionGetTypeGuid(fidl_txn_t* txn);
  zx_status_t FidlPartitionGetInstanceGuid(fidl_txn_t* txn);
  zx_status_t FidlPartitionGetName(fidl_txn_t* txn);
  zx_status_t FidlVolumeQuery(fidl_txn_t* txn);
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

  static const fuchsia_hardware_block_Block_ops* BlockOps() {
    using Binder = fidl::Binder<BlockDevice>;
    static const fuchsia_hardware_block_Block_ops kOps = {
        .GetInfo = Binder::BindMember<&BlockDevice::FidlBlockGetInfo>,
        .GetStats = Binder::BindMember<&BlockDevice::FidlBlockGetStats>,
        .GetFifo = Binder::BindMember<&BlockDevice::FidlBlockGetFifo>,
        .AttachVmo = Binder::BindMember<&BlockDevice::FidlBlockAttachVmo>,
        .CloseFifo = Binder::BindMember<&BlockDevice::FidlBlockCloseFifo>,
        .RebindDevice = Binder::BindMember<&BlockDevice::FidlBlockRebindDevice>,
    };
    return &kOps;
  }

  static const fuchsia_hardware_block_partition_Partition_ops* PartitionOps() {
    using Binder = fidl::Binder<BlockDevice>;
    static const fuchsia_hardware_block_partition_Partition_ops kOps = {
        .GetInfo = Binder::BindMember<&BlockDevice::FidlBlockGetInfo>,
        .GetStats = Binder::BindMember<&BlockDevice::FidlBlockGetStats>,
        .GetFifo = Binder::BindMember<&BlockDevice::FidlBlockGetFifo>,
        .AttachVmo = Binder::BindMember<&BlockDevice::FidlBlockAttachVmo>,
        .CloseFifo = Binder::BindMember<&BlockDevice::FidlBlockCloseFifo>,
        .RebindDevice = Binder::BindMember<&BlockDevice::FidlBlockRebindDevice>,
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
        .GetTypeGuid = Binder::BindMember<&BlockDevice::FidlPartitionGetTypeGuid>,
        .GetInstanceGuid = Binder::BindMember<&BlockDevice::FidlPartitionGetInstanceGuid>,
        .GetName = Binder::BindMember<&BlockDevice::FidlPartitionGetName>,
        .Query = Binder::BindMember<&BlockDevice::FidlVolumeQuery>,
        .QuerySlices = Binder::BindMember<&BlockDevice::FidlVolumeQuerySlices>,
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
  BlockDeviceMetrics stats_ TA_GUARDED(stat_lock_) = {};

  // To maintain stats related to time taken by a command or its success/failure, we need to
  // intercept command completion with a callback routine. This might introduce cpu
  // overhead.
  // TODO(auradkar): We should be able to turn on/off stats at run-time.
  //                 Create fidl interface to control how stats are maintained.
  bool completion_status_stats_ = true;
};

zx_status_t BlockDevice::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
  switch (proto_id) {
    case ZX_PROTOCOL_BLOCK: {
      self_protocol_.GetProto(static_cast<block_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_BLOCK_PARTITION: {
      if (!parent_partition_protocol_.is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      parent_partition_protocol_.GetProto(static_cast<block_partition_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_BLOCK_VOLUME: {
      if (!parent_volume_protocol_.is_valid()) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      parent_volume_protocol_.GetProto(static_cast<block_volume_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t BlockDevice::DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
  if (parent_volume_protocol_.is_valid()) {
    return fuchsia_hardware_block_volume_Volume_dispatch(this, txn, msg, VolumeOps());
  } else if (parent_partition_protocol_.is_valid()) {
    return fuchsia_hardware_block_partition_Partition_dispatch(this, txn, msg, PartitionOps());
  } else {
    return fuchsia_hardware_block_Block_dispatch(this, txn, msg, BlockOps());
  }
}

void BlockDevice::UpdateStats(bool success, zx::ticks start_tick, block_op_t* op) {
  uint64_t bytes_transfered = op->rw.length * info_.block_size;
  fbl::AutoLock lock(&stat_lock_);
  stats_.UpdateStats(success, start_tick, op->command, bytes_transfered);
}

// Adapter from read/write to block_op_t
// This is technically incorrect because the read/write hooks should not block,
// but the old adapter in devhost was *also* blocking, so we're no worse off
// than before, but now localized to the block middle layer.
// TODO(swetland) plumbing in devhosts to do deferred replies

// Define the maximum I/O possible for the midlayer; this is arbitrarily
// set to the size of RIO's max payload.
//
// If a smaller value of "max_transfer_size" is defined, that will
// be used instead.
constexpr uint32_t kMaxMidlayerIO = 8192;

zx_status_t BlockDevice::DoIo(void* buf, size_t buf_len, zx_off_t off, bool write) {
  fbl::AutoLock lock(&io_lock_);
  const size_t block_size = info_.block_size;
  const size_t max_xfer = std::min(info_.max_transfer_size, kMaxMidlayerIO);

  if (buf_len == 0) {
    return ZX_OK;
  }
  if ((buf_len % block_size) || (off % block_size)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!io_vmo_) {
    if (zx::vmo::create(std::max(max_xfer, static_cast<size_t>(PAGE_SIZE)), 0, &io_vmo_) != ZX_OK) {
      return ZX_ERR_INTERNAL;
    }
  }

  // TODO(smklein): These requests can be queued simultaneously without
  // blocking. However, as the comment above mentions, this code probably
  // shouldn't be blocking at all.
  uint64_t sub_txn_offset = 0;
  while (sub_txn_offset < buf_len) {
    void* sub_buf = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(buf) + sub_txn_offset);
    size_t sub_txn_length = std::min(buf_len - sub_txn_offset, max_xfer);

    if (write) {
      if (io_vmo_.write(sub_buf, 0, sub_txn_length) != ZX_OK) {
        return ZX_ERR_INTERNAL;
      }
    }
    block_op_t* op = reinterpret_cast<block_op_t*>(io_op_.get());
    op->command = write ? BLOCK_OP_WRITE : BLOCK_OP_READ;
    ZX_DEBUG_ASSERT(sub_txn_length / block_size < std::numeric_limits<uint32_t>::max());
    op->rw.length = static_cast<uint32_t>(sub_txn_length / block_size);
    op->rw.vmo = io_vmo_.get();
    op->rw.offset_dev = (off + sub_txn_offset) / block_size;
    op->rw.offset_vmo = 0;

    sync_completion_reset(&io_signal_);
    auto completion_cb = [](void* cookie, zx_status_t status, block_op_t* op) {
      BlockDevice* bdev = reinterpret_cast<BlockDevice*>(cookie);
      bdev->io_status_ = status;
      sync_completion_signal(&bdev->io_signal_);
    };

    BlockQueue(op, completion_cb, this);
    sync_completion_wait(&io_signal_, ZX_TIME_INFINITE);

    if (io_status_ != ZX_OK) {
      return io_status_;
    }

    if (!write) {
      if (io_vmo_.read(sub_buf, 0, sub_txn_length) != ZX_OK) {
        return ZX_ERR_INTERNAL;
      }
    }
    sub_txn_offset += sub_txn_length;
  }

  return io_status_;
}

zx_status_t BlockDevice::DdkRead(void* buf, size_t buf_len, zx_off_t off, size_t* actual) {
  zx_status_t status = DoIo(buf, buf_len, off, false);
  *actual = (status == ZX_OK) ? buf_len : 0;
  return status;
}

zx_status_t BlockDevice::DdkWrite(const void* buf, size_t buf_len, zx_off_t off, size_t* actual) {
  zx_status_t status = DoIo(const_cast<void*>(buf), buf_len, off, true);
  *actual = (status == ZX_OK) ? buf_len : 0;
  return status;
}

zx_off_t BlockDevice::DdkGetSize() { return device_get_size(parent()); }

void BlockDevice::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void BlockDevice::DdkRelease() { delete this; }

void BlockDevice::BlockQuery(block_info_t* block_info, size_t* op_size) {
  // It is important that all devices sitting on top of the volume protocol avoid
  // caching a copy of block info for query. The "block_count" field is dynamic,
  // and may change during the lifetime of the volume.
  size_t parent_op_size;
  parent_protocol_.Query(block_info, &parent_op_size);

  // Safety check that parent op size doesn't change dynamically.
  ZX_DEBUG_ASSERT(parent_op_size == parent_op_size_);

  *op_size = OpSize();
}

void BlockDevice::UpdateStatsAndCallCompletion(void* cookie, zx_status_t status, block_op_t* op) {
  BlockDevice* block_device = static_cast<BlockDevice*>(cookie);
  Transaction txn(op, block_device->parent_op_size_);
  StatsCookie* stats_cookie = txn.private_storage();

  block_device->UpdateStats(status == ZX_OK, stats_cookie->start_tick, op);
  txn.Complete(status);
}

void BlockDevice::BlockQueue(block_op_t* op, block_impl_queue_callback completion_cb,
                             void* cookie) {
  zx::ticks start_tick = zx::ticks::now();

  if (completion_status_stats_) {
    Transaction txn(op, completion_cb, cookie, parent_op_size_);
    StatsCookie* stats_cookie = txn.private_storage();
    stats_cookie->start_tick = start_tick;
    parent_protocol_.Queue(txn.take(), UpdateStatsAndCallCompletion, this);
  } else {
    // Since we don't know the return status, we assume all commands succeeded.
    UpdateStats(true, start_tick, op);
    parent_protocol_.Queue(op, completion_cb, cookie);
  }
}

void BlockDevice::ConvertToBlockStats(block_stats_t* out) {
  fuchsia_hardware_block_BlockStats metrics;
  stats_.CopyToFidl(&metrics);
  out->total_ops = stats_.TotalCalls();
  out->total_blocks = stats_.TotalBytesTransferred() / info_.block_size;
  out->total_reads = metrics.read.success.total_calls + metrics.read.failure.total_calls;
  out->total_blocks_read =
      (metrics.read.success.bytes_transferred + metrics.read.failure.bytes_transferred) /
      info_.block_size;
  out->total_writes = metrics.write.success.total_calls + metrics.write.failure.total_calls;
  out->total_blocks_written =
      (metrics.write.success.bytes_transferred + metrics.write.failure.bytes_transferred) /
      info_.block_size;
}

zx_status_t BlockDevice::GetStats(bool clear, block_stats_t* out) {
  fbl::AutoLock lock(&stat_lock_);

  if (stats_.Enabled()) {
    ConvertToBlockStats(out);
    if (clear) {
      stats_.Reset();
    }
    return ZX_OK;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t BlockDevice::FidlBlockGetInfo(fidl_txn_t* txn) {
  block_info_t info;
  size_t block_op_size = 0;
  parent_protocol_.Query(&info, &block_op_size);
  // Set or clear BLOCK_FLAG_BOOTPART appropriately.
  if (has_bootpart_) {
    info.flags |= BLOCK_FLAG_BOOTPART;
  } else {
    info.flags &= ~BLOCK_FLAG_BOOTPART;
  }

  static_assert(sizeof(block_info_t) == sizeof(fuchsia_hardware_block_BlockInfo),
                "Unsafe to cast between internal / FIDL types");

  return fuchsia_hardware_block_BlockGetInfo_reply(
      txn, ZX_OK, reinterpret_cast<const fuchsia_hardware_block_BlockInfo*>(&info));
}

zx_status_t BlockDevice::FidlBlockGetStats(bool clear, fidl_txn_t* txn) {
  fbl::AutoLock lock(&stat_lock_);
  if (!enable_stats_) {
    return fuchsia_hardware_block_BlockGetStats_reply(txn, ZX_ERR_NOT_SUPPORTED, nullptr);
  }

  fuchsia_hardware_block_BlockStats stats = {};
  stats_.CopyToFidl(&stats);
  if (clear) {
    stats_.Reset();
  }
  return fuchsia_hardware_block_BlockGetStats_reply(txn, ZX_OK, &stats);
}

zx_status_t BlockDevice::FidlBlockGetFifo(fidl_txn_t* txn) {
  zx::fifo fifo;
  zx_status_t status = manager_.StartServer(&self_protocol_, &fifo);
  return fuchsia_hardware_block_BlockGetFifo_reply(txn, status, fifo.release());
}

zx_status_t BlockDevice::FidlBlockAttachVmo(zx_handle_t vmo, fidl_txn_t* txn) {
  fuchsia_hardware_block_VmoId vmoid = {fuchsia_hardware_block_VMOID_INVALID};
  zx_status_t status = manager_.AttachVmo(zx::vmo(vmo), &vmoid.id);
  return fuchsia_hardware_block_BlockAttachVmo_reply(txn, status, &vmoid);
}

zx_status_t BlockDevice::FidlBlockCloseFifo(fidl_txn_t* txn) {
  return fuchsia_hardware_block_BlockCloseFifo_reply(txn, manager_.CloseFifoServer());
}

zx_status_t BlockDevice::FidlBlockRebindDevice(fidl_txn_t* txn) {
  return fuchsia_hardware_block_BlockRebindDevice_reply(txn, device_rebind(zxdev()));
}

zx_status_t BlockDevice::FidlPartitionGetTypeGuid(fidl_txn_t* txn) {
  fuchsia_hardware_block_partition_GUID guid;
  static_assert(sizeof(guid.value) == sizeof(guid_t), "Mismatched GUID size");
  guid_t* guid_ptr = reinterpret_cast<guid_t*>(&guid.value[0]);
  zx_status_t status = parent_partition_protocol_.GetGuid(GUIDTYPE_TYPE, guid_ptr);
  return fuchsia_hardware_block_partition_PartitionGetTypeGuid_reply(
      txn, status, status != ZX_OK ? nullptr : &guid);
}

zx_status_t BlockDevice::FidlPartitionGetInstanceGuid(fidl_txn_t* txn) {
  fuchsia_hardware_block_partition_GUID guid;
  static_assert(sizeof(guid.value) == sizeof(guid_t), "Mismatched GUID size");
  guid_t* guid_ptr = reinterpret_cast<guid_t*>(&guid.value[0]);
  zx_status_t status = parent_partition_protocol_.GetGuid(GUIDTYPE_INSTANCE, guid_ptr);
  return fuchsia_hardware_block_partition_PartitionGetInstanceGuid_reply(
      txn, status, status != ZX_OK ? nullptr : &guid);
}

zx_status_t BlockDevice::FidlPartitionGetName(fidl_txn_t* txn) {
  char name[fuchsia_hardware_block_partition_NAME_LENGTH];
  zx_status_t status = parent_partition_protocol_.GetName(name, sizeof(name));

  const char* out_name = nullptr;
  size_t out_name_length = 0;
  if (status == ZX_OK) {
    out_name = name;
    out_name_length = strnlen(name, sizeof(name));
  }
  return fuchsia_hardware_block_partition_PartitionGetName_reply(txn, status, out_name,
                                                                 out_name_length);
}

zx_status_t BlockDevice::FidlVolumeQuery(fidl_txn_t* txn) {
  fuchsia_hardware_block_volume_VolumeInfo info;
  static_assert(sizeof(parent_volume_info_t) == sizeof(info), "Mismatched volume info");
  zx_status_t status =
      parent_volume_protocol_.Query(reinterpret_cast<parent_volume_info_t*>(&info));
  return fuchsia_hardware_block_volume_VolumeQuery_reply(txn, status,
                                                         status != ZX_OK ? nullptr : &info);
}

zx_status_t BlockDevice::FidlVolumeQuerySlices(const uint64_t* start_slices_data,
                                               size_t start_slices_count, fidl_txn_t* txn) {
  fuchsia_hardware_block_volume_VsliceRange
      ranges[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
  memset(ranges, 0, sizeof(ranges));
  size_t range_count = 0;
  static_assert(sizeof(fuchsia_hardware_block_volume_VsliceRange) == sizeof(slice_region_t),
                "Mismatched range size");
  auto banjo_ranges = reinterpret_cast<slice_region_t*>(ranges);
  zx_status_t status = parent_volume_protocol_.QuerySlices(
      start_slices_data, start_slices_count, banjo_ranges, std::size(ranges), &range_count);
  return fuchsia_hardware_block_volume_VolumeQuerySlices_reply(txn, status, ranges, range_count);
}

zx_status_t BlockDevice::FidlVolumeExtend(uint64_t start_slice, uint64_t slice_count,
                                          fidl_txn_t* txn) {
  slice_extent_t extent;
  extent.offset = start_slice;
  extent.length = slice_count;
  zx_status_t status = parent_volume_protocol_.Extend(&extent);
  return fuchsia_hardware_block_volume_VolumeExtend_reply(txn, status);
}

zx_status_t BlockDevice::FidlVolumeShrink(uint64_t start_slice, uint64_t slice_count,
                                          fidl_txn_t* txn) {
  slice_extent_t extent;
  extent.offset = start_slice;
  extent.length = slice_count;
  zx_status_t status = parent_volume_protocol_.Shrink(&extent);
  return fuchsia_hardware_block_volume_VolumeShrink_reply(txn, status);
}

zx_status_t BlockDevice::FidlVolumeDestroy(fidl_txn_t* txn) {
  zx_status_t status = parent_volume_protocol_.Destroy();
  return fuchsia_hardware_block_volume_VolumeDestroy_reply(txn, status);
}

zx_status_t BlockDevice::Bind(void* ctx, zx_device_t* dev) {
  auto bdev = std::make_unique<BlockDevice>(dev);

  // The Block Implementation Protocol is required.
  if (!bdev->parent_protocol_.is_valid()) {
    printf("ERROR: block device '%s': does not support block protocol\n", device_get_name(dev));
    return ZX_ERR_NOT_SUPPORTED;
  }

  bdev->parent_protocol_.Query(&bdev->info_, &bdev->parent_op_size_);

  if (bdev->info_.max_transfer_size < bdev->info_.block_size) {
    printf("ERROR: block device '%s': has smaller max xfer (0x%x) than block size (0x%x)\n",
           device_get_name(dev), bdev->info_.max_transfer_size, bdev->info_.block_size);
    return ZX_ERR_NOT_SUPPORTED;
  }

  bdev->io_op_ = std::make_unique<uint8_t[]>(bdev->OpSize());
  size_t block_size = bdev->info_.block_size;
  if ((block_size < 512) || (block_size & (block_size - 1))) {
    printf("block: device '%s': invalid block size: %zu\n", device_get_name(dev), block_size);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // check to see if we have a ZBI partition map
  // and set BLOCK_FLAG_BOOTPART accordingly
  uint8_t buffer[METADATA_PARTITION_MAP_MAX];
  size_t actual;
  zx_status_t status =
      device_get_metadata(dev, DEVICE_METADATA_PARTITION_MAP, buffer, sizeof(buffer), &actual);
  if (status == ZX_OK && actual >= sizeof(zbi_partition_map_t)) {
    bdev->has_bootpart_ = true;
  }

  // We implement |ZX_PROTOCOL_BLOCK|, not |ZX_PROTOCOL_BLOCK_IMPL|. This is the
  // "core driver" protocol for block device drivers.
  status = bdev->DdkAdd("block");
  if (status != ZX_OK) {
    return status;
  }

  // The device has been added; we'll release it in blkdev_release.
  __UNUSED auto r = bdev.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t block_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = &BlockDevice::Bind;
  return ops;
}();

ZIRCON_DRIVER(block, block_driver_ops, "zircon", "0.1");
