// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <vector>

#include <block-client/cpp/fake-device.h>
#include <fbl/auto_lock.h>
#include <fvm/format.h>
#include <zxtest/zxtest.h>

namespace block_client {

FakeBlockDevice::FakeBlockDevice(uint64_t block_count, uint32_t block_size)
    : block_count_(block_count), block_size_(block_size) {
  ASSERT_OK(zx::vmo::create(block_count * block_size, ZX_VMO_RESIZABLE, &block_device_));
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

void FakeBlockDevice::SetInfoFlags(uint32_t flags) {
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
  return IsRegisteredLocked(vmoid);
}

bool FakeBlockDevice::IsRegisteredLocked(vmoid_t vmoid) const {
  return vmos_.find(vmoid) != vmos_.end();
}

void FakeBlockDevice::GetStats(bool clear, fuchsia_hardware_block_BlockStats* out_stats) {
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
  EXPECT_OK(block_device_.get_size(&size));
  if (size < new_size) {
    AdjustBlockDeviceSizeLocked(new_size);
  }
}

void FakeBlockDevice::AdjustBlockDeviceSizeLocked(uint64_t new_size) {
  EXPECT_OK(block_device_.set_size(new_size));
}

void FakeBlockDevice::UpdateStats(bool success, zx::ticks start_tick,
                                  const block_fifo_request_t& op) {
  stats_.UpdateStats(success, start_tick, op.opcode, block_size_ * op.length);
}

zx_status_t FakeBlockDevice::ReadBlock(uint64_t block_num, uint64_t fs_block_size,
                                       void* block) const {
  zx::ticks start_tick = zx::ticks::now();
  fbl::AutoLock lock(&lock_);
  zx_status_t status = block_device_.read(block, block_num * fs_block_size, fs_block_size);
  stats_.UpdateStats(status == ZX_OK, start_tick, BLOCKIO_READ, fs_block_size);
  return status;
}

zx_status_t FakeBlockDevice::FifoTransaction(block_fifo_request_t* requests, size_t count) {
  fbl::AutoLock lock(&lock_);
  for (size_t i = 0; i < count; i++) {
    zx::ticks start_tick = zx::ticks::now();
    switch (requests[i].opcode & BLOCKIO_OP_MASK) {
      case BLOCKIO_READ: {
        vmoid_t vmoid = requests[i].vmoid;
        auto& target_vmoid = vmos_[vmoid];
        uint8_t buffer[block_size_];
        memset(buffer, 0, block_size_);
        for (size_t j = 0; j < requests[i].length; j++) {
          uint64_t offset = (requests[i].dev_offset + j) * block_size_;
          EXPECT_OK(block_device_.read(buffer, offset, block_size_));
          offset = (requests[i].vmo_offset + j) * block_size_;
          EXPECT_OK(target_vmoid.write(buffer, offset, block_size_));
        }
        UpdateStats(true, start_tick, requests[i]);
        break;
      }
      case BLOCKIO_WRITE: {
        vmoid_t vmoid = requests[i].vmoid;
        auto& target_vmoid = vmos_[vmoid];
        uint8_t buffer[block_size_];
        memset(buffer, 0, block_size_);
        for (size_t j = 0; j < requests[i].length; j++) {
          if (write_block_limit_.has_value()) {
            if (write_block_count_ >= write_block_limit_) {
              return ZX_ERR_IO;
            }
          }
          uint64_t offset = (requests[i].vmo_offset + j) * block_size_;
          EXPECT_OK(target_vmoid.read(buffer, offset, block_size_));
          offset = (requests[i].dev_offset + j) * block_size_;
          EXPECT_OK(block_device_.write(buffer, offset, block_size_));
          write_block_count_++;
        }
        UpdateStats(true, start_tick, requests[i]);
        break;
      }
      case BLOCKIO_TRIM:
        UpdateStats(false, start_tick, requests[i]);
        return ZX_ERR_NOT_SUPPORTED;
      case BLOCKIO_FLUSH:
        UpdateStats(true, start_tick, requests[i]);
        continue;
      case BLOCKIO_CLOSE_VMO:
        EXPECT_TRUE(IsRegisteredLocked(requests[i].vmoid), "Closing unregistered VMO");
        vmos_.erase(requests[i].vmoid);
        break;
      default:
        UpdateStats(false, start_tick, requests[i]);
        return ZX_ERR_NOT_SUPPORTED;
    }
  }
  return ZX_OK;
}

zx_status_t FakeBlockDevice::BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const {
  fbl::AutoLock lock(&lock_);
  out_info->block_count = block_count_;
  out_info->block_size = block_size_;
  out_info->flags = block_info_flags_;
  return ZX_OK;
}

zx_status_t FakeBlockDevice::BlockAttachVmo(const zx::vmo& vmo,
                                            fuchsia_hardware_block_VmoID* out_vmoid) {
  zx::vmo xfer_vmo;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &xfer_vmo);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AutoLock lock(&lock_);
  vmos_.insert(std::make_pair(next_vmoid_, std::move(xfer_vmo)));
  out_vmoid->id = next_vmoid_++;
  return ZX_OK;
}

FakeFVMBlockDevice::FakeFVMBlockDevice(uint64_t block_count, uint32_t block_size,
                                       uint64_t slice_size, uint64_t slice_capacity)
    : FakeBlockDevice(block_count, block_size),
      slice_size_(slice_size),
      vslice_count_(fvm::kMaxVSlices) {
  extents_.emplace(0, range::Range<uint64_t>(0, 1));
  pslice_allocated_count_++;
  EXPECT_GE(slice_capacity, pslice_allocated_count_);
  pslice_total_count_ = slice_capacity;
}

zx_status_t FakeFVMBlockDevice::FifoTransaction(block_fifo_request_t* requests, size_t count) {
  fbl::AutoLock lock(&lock_);

  fuchsia_hardware_block_BlockInfo info = {};
  EXPECT_OK(BlockGetInfo(&info));
  EXPECT_GE(slice_size_, info.block_size, "Slice size must be larger than block size");
  EXPECT_EQ(0, slice_size_ % info.block_size, "Slice size not divisible by block size");

  size_t blocks_per_slice = slice_size_ / info.block_size;

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
    EXPECT_NE(extent, extents_.end(), "Could not find matching slices for operation");
    EXPECT_LE(extent->second.Start(), range.Start(),
              "Operation does not start within allocated slice");
    EXPECT_GE(extent->second.End(), range.End(), "Operation does not end within allocated slice");
  }

  return FakeBlockDevice::FifoTransaction(requests, count);
}

zx_status_t FakeFVMBlockDevice::VolumeQuery(
    fuchsia_hardware_block_volume_VolumeInfo* out_info) const {
  out_info->slice_size = slice_size_;
  out_info->vslice_count = vslice_count_;
  fbl::AutoLock lock(&lock_);
  out_info->pslice_total_count = pslice_total_count_;
  out_info->pslice_allocated_count = pslice_allocated_count_;
  return ZX_OK;
}

zx_status_t FakeFVMBlockDevice::VolumeQuerySlices(
    const uint64_t* slices, size_t slices_count,
    fuchsia_hardware_block_volume_VsliceRange* out_ranges, size_t* out_ranges_count) const {
  *out_ranges_count = 0;
  fbl::AutoLock lock(&lock_);
  for (size_t i = 0; i < slices_count; i++) {
    uint64_t slice_start = slices[i];
    if (slice_start >= vslice_count_) {
      // Out-of-range.
      return ZX_ERR_OUT_OF_RANGE;
    }

    auto extent = extents_.lower_bound(slice_start);
    if (extent == extents_.end() || extent->first != slice_start) {
      extent--;
    }
    EXPECT_NE(extent, extents_.end());
    if (extent->second.Start() <= slice_start && slice_start < extent->second.End()) {
      // Allocated.
      out_ranges[*out_ranges_count].allocated = true;
      out_ranges[*out_ranges_count].count = extent->second.End() - slice_start;
    } else {
      // Not allocated.
      out_ranges[*out_ranges_count].allocated = false;

      extent++;
      if (extent == extents_.end()) {
        out_ranges[*out_ranges_count].count = vslice_count_ - slice_start;
      } else {
        out_ranges[*out_ranges_count].count = extent->second.Start() - slice_start;
      }
    }
    (*out_ranges_count)++;
  }
  return ZX_OK;
}

zx_status_t FakeFVMBlockDevice::VolumeExtend(uint64_t offset, uint64_t length) {
  fbl::AutoLock lock(&lock_);
  if (offset + length > vslice_count_) {
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
      EXPECT_GE(new_slices, overlap_length, "underflow");
      new_slices -= overlap_length;
    }
  }

  if (new_slices > pslice_total_count_ - pslice_allocated_count_) {
    return ZX_ERR_NO_SPACE;
  }

  // Actually make modifications.
  for (auto& start : merged_starts) {
    extents_.erase(start);
  }
  extents_.emplace(extension.Start(), extension);
  pslice_allocated_count_ += new_slices;
  ResizeDeviceToAtLeast(extension.End() * slice_size_);
  return ZX_OK;
}

zx_status_t FakeFVMBlockDevice::VolumeShrink(uint64_t offset, uint64_t length) {
  fbl::AutoLock lock(&lock_);
  if (offset + length > vslice_count_) {
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
          EXPECT_TRUE(end_overlap);
          new_start = iter->second.Start();
          new_end = range.Start();
        }
        range::Range<uint64_t> new_extent(new_start, new_end);
        erased_blocks += iter->second.Length() - new_extent.Length();
        iter = extents_.erase(iter);
        extents_.emplace(new_start, std::move(new_extent));
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
  EXPECT_GE(pslice_allocated_count_, erased_blocks);
  pslice_allocated_count_ -= erased_blocks;
  return ZX_OK;
}

}  // namespace block_client
