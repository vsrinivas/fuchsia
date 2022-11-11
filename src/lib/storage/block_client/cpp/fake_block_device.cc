// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/block_client/cpp/fake_block_device.h"

#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <vector>

#include <fbl/auto_lock.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/storage/fvm/format.h"

namespace block_client {

FakeBlockDevice::FakeBlockDevice(const FakeBlockDevice::Config& config)
    : block_count_(config.block_count),
      block_size_(config.block_size),
      max_transfer_size_(config.max_transfer_size) {
  ZX_ASSERT(zx::vmo::create(block_count_ * block_size_, ZX_VMO_RESIZABLE, &block_device_) == ZX_OK);
  ZX_ASSERT(max_transfer_size_ == fuchsia_hardware_block::wire::kMaxTransferUnbounded ||
            max_transfer_size_ % block_size_ == 0);
  if (config.supports_trim) {
    block_info_flags_ |= fuchsia_hardware_block::wire::Flag::kTrimSupport;
  }
}

void FakeBlockDevice::Pause() {
  fbl::AutoLock lock(&lock_);
  paused_ = true;
}

void FakeBlockDevice::Resume() {
  fbl::AutoLock lock(&lock_);
  paused_ = false;
  pause_condition_.Broadcast();
}

void FakeBlockDevice::SetWriteBlockLimit(uint64_t limit) {
  fbl::AutoLock lock(&lock_);
  write_block_limit_ = limit;
}

void FakeBlockDevice::ResetWriteBlockLimit() {
  fbl::AutoLock lock(&lock_);
  write_block_limit_ = std::nullopt;
}

uint64_t FakeBlockDevice::GetWriteBlockCount() const {
  fbl::AutoLock lock(&lock_);
  return write_block_count_;
}

void FakeBlockDevice::ResetBlockCounts() {
  fbl::AutoLock lock(&lock_);
  write_block_count_ = 0;
}

void FakeBlockDevice::SetInfoFlags(fuchsia_hardware_block::wire::Flag flags) {
  fbl::AutoLock lock(&lock_);
  block_info_flags_ = flags;
}

void FakeBlockDevice::SetBlockCount(uint64_t block_count) {
  fbl::AutoLock lock(&lock_);
  block_count_ = block_count;
  AdjustBlockDeviceSizeLocked(block_count_ * block_size_);
}

void FakeBlockDevice::SetBlockSize(uint32_t block_size) {
  fbl::AutoLock lock(&lock_);
  block_size_ = block_size;
  AdjustBlockDeviceSizeLocked(block_count_ * block_size_);
}

bool FakeBlockDevice::IsRegistered(vmoid_t vmoid) const {
  fbl::AutoLock lock(&lock_);
  return vmos_.find(vmoid) != vmos_.end();
}

void FakeBlockDevice::GetStats(bool clear, fuchsia_hardware_block::wire::BlockStats* out_stats) {
  ZX_ASSERT(out_stats != nullptr);
  fbl::AutoLock lock(&lock_);
  stats_.CopyToFidl(out_stats);
  if (clear) {
    stats_.Reset();
  }
}

void FakeBlockDevice::ResizeDeviceToAtLeast(uint64_t new_size) {
  fbl::AutoLock lock(&lock_);
  uint64_t size;
  ZX_ASSERT(block_device_.get_size(&size) == ZX_OK);
  if (size < new_size) {
    AdjustBlockDeviceSizeLocked(new_size);
  }
}

void FakeBlockDevice::AdjustBlockDeviceSizeLocked(uint64_t new_size) {
  ZX_ASSERT(block_device_.set_size(new_size) == ZX_OK);
}

void FakeBlockDevice::UpdateStats(bool success, zx::ticks start_tick,
                                  const block_fifo_request_t& op) {
  stats_.UpdateStats(success, start_tick, op.opcode, block_size_ * op.length);
}

void FakeBlockDevice::WaitOnPaused() const __TA_REQUIRES(lock_) {
  while (paused_)
    pause_condition_.Wait(&lock_);
}

zx_status_t FakeBlockDevice::FifoTransaction(block_fifo_request_t* requests, size_t count) {
  fbl::AutoLock lock(&lock_);
  const uint32_t block_size = block_size_;
  for (size_t i = 0; i < count; i++) {
    // Allow pauses to take effect between each issued operation. This will potentially allow other
    // threads to issue transactions since it releases the lock, just as the actual implementation
    // does.
    WaitOnPaused();

    if (hook_) {
      auto iter = vmos_.find(requests[i].vmoid);
      if (zx_status_t status = hook_(requests[i], iter == vmos_.end() ? nullptr : &iter->second);
          status != ZX_OK) {
        return status;
      }
    }

    zx::ticks start_tick = zx::ticks::now();
    switch (requests[i].opcode & BLOCKIO_OP_MASK) {
      case BLOCKIO_READ: {
        vmoid_t vmoid = requests[i].vmoid;
        zx::vmo& target_vmoid = vmos_.at(vmoid);
        uint8_t buffer[block_size];
        memset(buffer, 0, block_size);
        for (size_t j = 0; j < requests[i].length; j++) {
          uint64_t offset = (requests[i].dev_offset + j) * block_size;
          zx_status_t status = block_device_.read(buffer, offset, block_size);
          if (status != ZX_OK) {
            FX_LOGS(ERROR) << "Read from device failed: offset=" << offset
                           << ", block_size=" << block_size;
            return status;
          }
          offset = (requests[i].vmo_offset + j) * block_size;
          status = target_vmoid.write(buffer, offset, block_size);
          if (status != ZX_OK) {
            FX_LOGS(ERROR) << "Write to buffer failed: offset=" << offset
                           << ", block_size=" << block_size;
            return status;
          }
        }
        UpdateStats(true, start_tick, requests[i]);
        break;
      }
      case BLOCKIO_WRITE: {
        vmoid_t vmoid = requests[i].vmoid;
        zx::vmo& target_vmoid = vmos_.at(vmoid);
        uint8_t buffer[block_size];
        memset(buffer, 0, block_size);
        for (size_t j = 0; j < requests[i].length; j++) {
          if (write_block_limit_.has_value()) {
            if (write_block_count_ >= write_block_limit_) {
              return ZX_ERR_IO;
            }
          }
          uint64_t offset = (requests[i].vmo_offset + j) * block_size;
          zx_status_t status = target_vmoid.read(buffer, offset, block_size);
          if (status != ZX_OK) {
            FX_LOGS(ERROR) << "Read from buffer failed: offset=" << offset
                           << ", block_size=" << block_size;
            return status;
          }
          offset = (requests[i].dev_offset + j) * block_size;
          status = block_device_.write(buffer, offset, block_size);
          if (status != ZX_OK) {
            FX_LOGS(ERROR) << "Write to device failed: offset=" << offset
                           << ", block_size=" << block_size;
            return status;
          }
          write_block_count_++;
        }
        UpdateStats(true, start_tick, requests[i]);
        break;
      }
      case BLOCKIO_TRIM:
        UpdateStats(false, start_tick, requests[i]);
        if (!(block_info_flags_ & fuchsia_hardware_block::wire::Flag::kTrimSupport)) {
          return ZX_ERR_NOT_SUPPORTED;
        }
        if (requests[i].vmoid != BLOCK_VMOID_INVALID) {
          return ZX_ERR_INVALID_ARGS;
        }
        if (requests[i].dev_offset + requests[i].length > block_count_) {
          return ZX_ERR_OUT_OF_RANGE;
        }
        break;
      case BLOCKIO_FLUSH:
        UpdateStats(true, start_tick, requests[i]);
        continue;
      case BLOCKIO_CLOSE_VMO:
        ZX_ASSERT(vmos_.erase(requests[i].vmoid) == 1);
        break;
      default:
        UpdateStats(false, start_tick, requests[i]);
        return ZX_ERR_NOT_SUPPORTED;
    }
  }
  return ZX_OK;
}

zx_status_t FakeBlockDevice::BlockGetInfo(fuchsia_hardware_block::wire::BlockInfo* out_info) const {
  fbl::AutoLock lock(&lock_);
  out_info->block_count = block_count_;
  out_info->block_size = block_size_;
  out_info->flags = static_cast<uint32_t>(block_info_flags_);
  out_info->max_transfer_size = max_transfer_size_;
  return ZX_OK;
}

void FakeBlockDevice::Wipe() {
  fbl::AutoLock lock(&lock_);
  ZX_ASSERT(block_device_.op_range(ZX_VMO_OP_ZERO, 0, block_count_ * block_size_, nullptr, 0) ==
            ZX_OK);
}

zx_status_t FakeBlockDevice::BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out_vmoid) {
  zx::vmo xfer_vmo;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &xfer_vmo);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AutoLock lock(&lock_);
  // Find a free vmoid.
  vmoid_t vmoid = 1;
  for (const auto& [used_vmoid, vmo] : vmos_) {
    if (used_vmoid > vmoid)
      break;
    if (used_vmoid == std::numeric_limits<vmoid_t>::max())
      return ZX_ERR_NO_RESOURCES;
    vmoid = used_vmoid + 1;
  }
  vmos_.insert(std::make_pair(vmoid, std::move(xfer_vmo)));
  *out_vmoid = storage::Vmoid(vmoid);
  return ZX_OK;
}

FakeFVMBlockDevice::FakeFVMBlockDevice(uint64_t block_count, uint32_t block_size,
                                       uint64_t slice_size, uint64_t slice_capacity)
    : FakeBlockDevice(block_count, block_size) {
  fbl::AutoLock lock(&fvm_lock_);
  manager_info_.slice_size = slice_size;
  manager_info_.slice_count = slice_capacity;
  manager_info_.assigned_slice_count = 1;
  manager_info_.max_virtual_slice = fvm::kMaxVSlices;

  volume_info_.partition_slice_count = manager_info_.assigned_slice_count;
  volume_info_.slice_limit = 0;

  extents_.emplace(0, range::Range<uint64_t>(0, 1));
  ZX_ASSERT(slice_capacity >= manager_info_.assigned_slice_count);
}

zx_status_t FakeFVMBlockDevice::FifoTransaction(block_fifo_request_t* requests, size_t count) {
  fbl::AutoLock lock(&fvm_lock_);
  // Don't need WaitOnPaused() here because this code just validates the input. The actual
  // requests will be excuted by the FakeBlockDevice::FifoTransaction() call at the bottom which
  // handles the pause requests.

  fuchsia_hardware_block::wire::BlockInfo info = {};
  ZX_ASSERT(BlockGetInfo(&info) == ZX_OK);
  ZX_ASSERT_MSG(manager_info_.slice_size >= info.block_size,
                "Slice size must be larger than block size");
  ZX_ASSERT_MSG(manager_info_.slice_size % info.block_size == 0,
                "Slice size not divisible by block size");

  size_t blocks_per_slice = manager_info_.slice_size / info.block_size;

  // Validate that the operation acts on valid slices before sending it to the underlying
  // mock device.
  for (size_t i = 0; i < count; i++) {
    switch (requests[i].opcode & BLOCKIO_OP_MASK) {
      case BLOCKIO_READ:
        break;
      case BLOCKIO_WRITE:
        break;
      case BLOCKIO_TRIM:
        break;
      default:
        continue;
    }
    uint64_t dev_start = requests[i].dev_offset;
    uint64_t length = requests[i].length;

    uint64_t start_slice = dev_start / blocks_per_slice;
    uint64_t slice_length = fbl::round_up(length, blocks_per_slice) / blocks_per_slice;
    range::Range<uint64_t> range(start_slice, start_slice + slice_length);
    auto extent = extents_.lower_bound(range.Start());
    if (extent == extents_.end() || extent->first != range.Start()) {
      extent--;
    }
    ZX_ASSERT_MSG(extent != extents_.end(), "Could not find matching slices for operation");
    ZX_ASSERT_MSG(extent->second.Start() <= range.Start(),
                  "Operation does not start within allocated slice");
    ZX_ASSERT_MSG(extent->second.End() >= range.End(),
                  "Operation does not end within allocated slice");
  }

  return FakeBlockDevice::FifoTransaction(requests, count);
}

zx_status_t FakeFVMBlockDevice::VolumeGetInfo(
    fuchsia_hardware_block_volume::wire::VolumeManagerInfo* out_manager_info,
    fuchsia_hardware_block_volume::wire::VolumeInfo* out_volume_info) const {
  fbl::AutoLock lock(&fvm_lock_);
  *out_manager_info = manager_info_;
  *out_volume_info = volume_info_;
  return ZX_OK;
}

zx_status_t FakeFVMBlockDevice::VolumeQuerySlices(
    const uint64_t* slices, size_t slices_count,
    fuchsia_hardware_block_volume::wire::VsliceRange* out_ranges, size_t* out_ranges_count) const {
  *out_ranges_count = 0;
  fbl::AutoLock lock(&fvm_lock_);
  for (size_t i = 0; i < slices_count; i++) {
    uint64_t slice_start = slices[i];
    if (slice_start >= manager_info_.max_virtual_slice) {
      // Out-of-range.
      return ZX_ERR_OUT_OF_RANGE;
    }

    auto extent = extents_.lower_bound(slice_start);
    if (extent == extents_.end() || extent->first != slice_start) {
      extent--;
    }
    ZX_ASSERT(extent != extents_.end());
    if (extent->second.Start() <= slice_start && slice_start < extent->second.End()) {
      // Allocated.
      out_ranges[*out_ranges_count].allocated = true;
      out_ranges[*out_ranges_count].count = extent->second.End() - slice_start;
    } else {
      // Not allocated.
      out_ranges[*out_ranges_count].allocated = false;

      extent++;
      if (extent == extents_.end()) {
        out_ranges[*out_ranges_count].count = manager_info_.max_virtual_slice - slice_start;
      } else {
        out_ranges[*out_ranges_count].count = extent->second.Start() - slice_start;
      }
    }
    (*out_ranges_count)++;
  }
  return ZX_OK;
}

zx_status_t FakeFVMBlockDevice::VolumeExtend(uint64_t offset, uint64_t length) {
  fbl::AutoLock lock(&fvm_lock_);
  if (offset + length > manager_info_.max_virtual_slice) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (length == 0) {
    return ZX_OK;
  }

  uint64_t new_slices = length;
  std::vector<uint64_t> merged_starts;

  range::Range<uint64_t> extension(offset, offset + length);
  for (auto& range : extents_) {
    if (Mergable(extension, range.second)) {
      // Track this location; we'll need to remove it later.
      //
      // Avoid removing it now in case we don't have enough space.
      merged_starts.push_back(range.first);
      uint64_t total_length = extension.Length() + range.second.Length();
      extension.Merge(range.second);
      uint64_t merged_length = extension.Length();
      uint64_t overlap_length = total_length - merged_length;
      ZX_ASSERT_MSG(new_slices >= overlap_length, "underflow");
      new_slices -= overlap_length;
    }
  }

  if (new_slices > manager_info_.slice_count - manager_info_.assigned_slice_count) {
    return ZX_ERR_NO_SPACE;
  }

  // Actually make modifications.
  for (auto& start : merged_starts) {
    extents_.erase(start);
  }
  extents_.emplace(extension.Start(), extension);
  manager_info_.assigned_slice_count += new_slices;
  volume_info_.partition_slice_count = manager_info_.assigned_slice_count;
  ResizeDeviceToAtLeast(extension.End() * manager_info_.slice_size);
  return ZX_OK;
}

zx_status_t FakeFVMBlockDevice::VolumeShrink(uint64_t offset, uint64_t length) {
  fbl::AutoLock lock(&fvm_lock_);
  if (offset + length > manager_info_.max_virtual_slice) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (length == 0) {
    return ZX_OK;
  }

  uint64_t erased_blocks = 0;
  range::Range<uint64_t> range(offset, offset + length);
  auto iter = extents_.begin();
  while (iter != extents_.end()) {
    if (Overlap(range, iter->second)) {
      bool start_overlap = range.Start() <= iter->second.Start();
      bool end_overlap = iter->second.End() <= range.End();

      if (start_overlap && end_overlap) {
        // Case 1: The overlap is total. The extent should be entirely removed.
        erased_blocks += iter->second.Length();
        iter = extents_.erase(iter);
      } else if (start_overlap || end_overlap) {
        // Case 2: The overlap is partial. The extent should be updated; either
        // moving forward the start or moving back the end.
        uint64_t new_start;
        uint64_t new_end;
        if (start_overlap) {
          new_start = range.End();
          new_end = iter->second.End();
        } else {
          ZX_ASSERT(end_overlap);
          new_start = iter->second.Start();
          new_end = range.Start();
        }
        range::Range<uint64_t> new_extent(new_start, new_end);
        erased_blocks += iter->second.Length() - new_extent.Length();
        iter = extents_.erase(iter);
        extents_.emplace(new_start, new_extent);
      } else {
        // Case 3: The overlap splits the extent in two.
        range::Range<uint64_t> before(iter->second.Start(), range.Start());
        range::Range<uint64_t> after(range.End(), iter->second.End());
        erased_blocks += iter->second.Length() - (before.Length() + after.Length());
        iter = extents_.erase(iter);
        extents_.emplace(before.Start(), before);
        extents_.emplace(after.Start(), after);
      }
    } else {
      // Case 4: There is no overlap.
      iter++;
    }
  }

  if (erased_blocks == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  ZX_ASSERT(manager_info_.assigned_slice_count >= erased_blocks);
  manager_info_.assigned_slice_count -= erased_blocks;
  volume_info_.partition_slice_count = manager_info_.assigned_slice_count;
  return ZX_OK;
}

}  // namespace block_client
