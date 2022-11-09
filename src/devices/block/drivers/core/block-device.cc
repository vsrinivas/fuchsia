// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/core/block-device.h"

#include <fuchsia/hardware/block/partition/cpp/banjo.h>
#include <lib/fidl-utils/bind.h>
#include <lib/operation/block.h>

#include <storage-metrics/block-metrics.h>

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

zx_status_t BlockDevice::DoIo(zx::vmo& vmo, size_t buf_len, zx_off_t off, zx_off_t vmo_off,
                              bool write) {
  fbl::AutoLock lock(&io_lock_);
  const size_t block_size = info_.block_size;
  const size_t max_xfer = std::min(info_.max_transfer_size, kMaxMidlayerIO);

  if (buf_len == 0) {
    return ZX_OK;
  }
  if ((buf_len % block_size) || (off % block_size)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // TODO(smklein): These requests can be queued simultaneously without
  // blocking. However, as the comment above mentions, this code probably
  // shouldn't be blocking at all.
  uint64_t sub_txn_offset = 0;
  while (sub_txn_offset < buf_len) {
    size_t sub_txn_length = std::min(buf_len - sub_txn_offset, max_xfer);

    block_op_t* op = reinterpret_cast<block_op_t*>(io_op_.get());
    op->command = write ? BLOCK_OP_WRITE : BLOCK_OP_READ;
    ZX_DEBUG_ASSERT(sub_txn_length / block_size < std::numeric_limits<uint32_t>::max());
    op->rw.length = static_cast<uint32_t>(sub_txn_length / block_size);
    op->rw.vmo = vmo.get();
    op->rw.offset_dev = (off + sub_txn_offset) / block_size;
    op->rw.offset_vmo = (vmo_off + sub_txn_offset) / block_size;

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

    sub_txn_offset += sub_txn_length;
  }

  return io_status_;
}

zx_status_t BlockDevice::DdkRead(void* buf, size_t buf_len, zx_off_t off, size_t* actual) {
  zx::vmo vmo;
  if (zx::vmo::create(std::max(buf_len, static_cast<size_t>(zx_system_get_page_size())), 0, &vmo) !=
      ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  zx_status_t status = DoIo(vmo, buf_len, off, 0, false);
  if (vmo.read(buf, 0, buf_len) != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  *actual = (status == ZX_OK) ? buf_len : 0;
  return status;
}

zx_status_t BlockDevice::DdkWrite(const void* buf, size_t buf_len, zx_off_t off, size_t* actual) {
  zx::vmo vmo;
  if (zx::vmo::create(std::max(buf_len, static_cast<size_t>(zx_system_get_page_size())), 0, &vmo) !=
      ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  if (vmo.write(buf, 0, buf_len) != ZX_OK) {
    return ZX_ERR_INTERNAL;
  }
  zx_status_t status = DoIo(vmo, buf_len, off, 0, true);
  *actual = (status == ZX_OK) ? buf_len : 0;
  return status;
}

zx_off_t BlockDevice::DdkGetSize() { return device_get_size(parent()); }

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
  block::BorrowedOperation<StatsCookie> txn(op, block_device->parent_op_size_);
  StatsCookie* stats_cookie = txn.private_storage();

  block_device->UpdateStats(status == ZX_OK, stats_cookie->start_tick, op);
  txn.Complete(status);
}

void BlockDevice::BlockQueue(block_op_t* op, block_impl_queue_callback completion_cb,
                             void* cookie) {
  zx::ticks start_tick = zx::ticks::now();

  if (completion_status_stats_) {
    block::BorrowedOperation<StatsCookie> txn(op, completion_cb, cookie, parent_op_size_);
    StatsCookie* stats_cookie = txn.private_storage();
    stats_cookie->start_tick = start_tick;
    parent_protocol_.Queue(txn.take(), UpdateStatsAndCallCompletion, this);
  } else {
    // Since we don't know the return status, we assume all commands succeeded.
    UpdateStats(true, start_tick, op);
    parent_protocol_.Queue(op, completion_cb, cookie);
  }
}

void BlockDevice::GetInfo(GetInfoCompleter::Sync& completer) {
  fuchsia_hardware_block::wire::BlockInfo info;
  static_assert(sizeof(info) == sizeof(block_info_t));
  size_t block_op_size;
  parent_protocol_.Query(reinterpret_cast<block_info_t*>(&info), &block_op_size);
  // Set or clear BLOCK_FLAG_BOOTPART appropriately.
  if (has_bootpart_) {
    info.flags |= BLOCK_FLAG_BOOTPART;
  } else {
    info.flags &= ~BLOCK_FLAG_BOOTPART;
  }

  completer.Reply(ZX_OK, fidl::ObjectView<decltype(info)>::FromExternal(&info));
}

void BlockDevice::GetStats(GetStatsRequestView request, GetStatsCompleter::Sync& completer) {
  fbl::AutoLock lock(&stat_lock_);
  if (!enable_stats_) {
    completer.Reply(ZX_ERR_NOT_SUPPORTED, nullptr);
    return;
  }

  fuchsia_hardware_block::wire::BlockStats stats;
  stats_.CopyToFidl(&stats);
  if (request->clear) {
    stats_.Reset();
  }
  completer.Reply(ZX_OK,
                  fidl::ObjectView<fuchsia_hardware_block::wire::BlockStats>::FromExternal(&stats));
}

void BlockDevice::GetFifo(GetFifoCompleter::Sync& completer) {
  zx::fifo fifo;
  zx_status_t status = manager_.StartServer(zxdev(), &self_protocol_, &fifo);
  completer.Reply(status, std::move(fifo));
}

void BlockDevice::AttachVmo(AttachVmoRequestView request, AttachVmoCompleter::Sync& completer) {
  fuchsia_hardware_block::wire::VmoId vmoid;
  zx_status_t status = manager_.AttachVmo(std::move(request->vmo), &vmoid.id);
  completer.Reply(status,
                  fidl::ObjectView<fuchsia_hardware_block::wire::VmoId>::FromExternal(&vmoid));
}

void BlockDevice::CloseFifo(CloseFifoCompleter::Sync& completer) {
  completer.Reply(manager_.CloseFifoServer());
}

void BlockDevice::RebindDevice(RebindDeviceCompleter::Sync& completer) {
  completer.Reply(device_rebind(zxdev()));
}

void BlockDevice::ReadBlocks(ReadBlocksRequestView request, ReadBlocksCompleter::Sync& completer) {
  completer.Reply(
      DoIo(request->vmo, request->length, request->dev_offset, request->vmo_offset, false));
}

void BlockDevice::WriteBlocks(WriteBlocksRequestView request,
                              WriteBlocksCompleter::Sync& completer) {
  completer.Reply(
      DoIo(request->vmo, request->length, request->dev_offset, request->vmo_offset, true));
}

void BlockDevice::GetTypeGuid(GetTypeGuidCompleter::Sync& completer) {
  if (!parent_partition_protocol_.is_valid()) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  fuchsia_hardware_block_partition::wire::Guid guid;
  static_assert(sizeof(guid.value) == sizeof(guid_t));
  guid_t* guid_ptr = reinterpret_cast<guid_t*>(guid.value.data());
  zx_status_t status = parent_partition_protocol_.GetGuid(GUIDTYPE_TYPE, guid_ptr);
  completer.Reply(status, fidl::ObjectView<decltype(guid)>::FromExternal(&guid));
}

void BlockDevice::GetInstanceGuid(GetInstanceGuidCompleter::Sync& completer) {
  if (!parent_partition_protocol_.is_valid()) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  fuchsia_hardware_block_partition::wire::Guid guid;
  static_assert(sizeof(guid.value) == sizeof(guid_t));
  guid_t* guid_ptr = reinterpret_cast<guid_t*>(guid.value.data());
  zx_status_t status = parent_partition_protocol_.GetGuid(GUIDTYPE_INSTANCE, guid_ptr);
  completer.Reply(status, fidl::ObjectView<decltype(guid)>::FromExternal(&guid));
}

void BlockDevice::GetName(GetNameCompleter::Sync& completer) {
  if (!parent_partition_protocol_.is_valid()) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  char name[fuchsia_hardware_block_partition::wire::kNameLength];
  zx_status_t status = parent_partition_protocol_.GetName(name, sizeof(name));
  completer.Reply(status,
                  status == ZX_OK ? fidl::StringView::FromExternal(name) : fidl::StringView{});
}

void BlockDevice::QuerySlices(QuerySlicesRequestView request,
                              QuerySlicesCompleter::Sync& completer) {
  if (!parent_volume_protocol_.is_valid()) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  fidl::Array<fuchsia_hardware_block_volume::wire::VsliceRange,
              fuchsia_hardware_block_volume::wire::kMaxSliceRequests>
      ranges;
  static_assert(sizeof(decltype(ranges)::value_type) == sizeof(slice_region_t));
  slice_region_t* ranges_ptr = reinterpret_cast<slice_region_t*>(ranges.data());
  size_t range_count;
  zx_status_t status = parent_volume_protocol_.QuerySlices(
      request->start_slices.data(), request->start_slices.count(), ranges_ptr, std::size(ranges),
      &range_count);
  completer.Reply(status, ranges, range_count);
}

void BlockDevice::GetVolumeInfo(GetVolumeInfoCompleter::Sync& completer) {
  if (!parent_volume_protocol_.is_valid()) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info;
  static_assert(sizeof(manager_info) == sizeof(volume_manager_info_t));
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info;
  static_assert(sizeof(volume_info) == sizeof(volume_info_t));
  zx_status_t status =
      parent_volume_protocol_.GetInfo(reinterpret_cast<volume_manager_info_t*>(&manager_info),
                                      reinterpret_cast<volume_info_t*>(&volume_info));
  fidl::ObjectView<decltype(manager_info)> manager_info_view;
  fidl::ObjectView<decltype(volume_info)> volume_info_view;
  if (status == ZX_OK) {
    manager_info_view = decltype(manager_info_view)::FromExternal(&manager_info);
    volume_info_view = decltype(volume_info_view)::FromExternal(&volume_info);
  }
  completer.Reply(status, manager_info_view, volume_info_view);
}

void BlockDevice::Extend(ExtendRequestView request, ExtendCompleter::Sync& completer) {
  if (!parent_volume_protocol_.is_valid()) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  slice_extent_t extent = {
      .offset = request->start_slice,
      .length = request->slice_count,
  };
  completer.Reply(parent_volume_protocol_.Extend(&extent));
}

void BlockDevice::Shrink(ShrinkRequestView request, ShrinkCompleter::Sync& completer) {
  if (!parent_volume_protocol_.is_valid()) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  slice_extent_t extent = {
      .offset = request->start_slice,
      .length = request->slice_count,
  };
  completer.Reply(parent_volume_protocol_.Shrink(&extent));
}

void BlockDevice::Destroy(DestroyCompleter::Sync& completer) {
  if (!parent_volume_protocol_.is_valid()) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  completer.Reply(parent_volume_protocol_.Destroy());
}

zx_status_t BlockDevice::Bind(void* ctx, zx_device_t* dev) {
  auto bdev = std::make_unique<BlockDevice>(dev);

  // The Block Implementation Protocol is required.
  if (!bdev->parent_protocol_.is_valid()) {
    printf("ERROR: block device: does not support block protocol\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  bdev->parent_protocol_.Query(&bdev->info_, &bdev->parent_op_size_);

  if (bdev->info_.max_transfer_size < bdev->info_.block_size) {
    printf("ERROR: block device: has smaller max xfer (0x%x) than block size (0x%x)\n",
           bdev->info_.max_transfer_size, bdev->info_.block_size);
    return ZX_ERR_NOT_SUPPORTED;
  }

  bdev->io_op_ = std::make_unique<uint8_t[]>(bdev->OpSize());
  size_t block_size = bdev->info_.block_size;
  if ((block_size < 512) || (block_size & (block_size - 1))) {
    printf("block: device: invalid block size: %zu\n", block_size);
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
