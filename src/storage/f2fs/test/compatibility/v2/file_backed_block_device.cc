// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include "src/storage/f2fs/f2fs.h"
#include "src/storage/f2fs/test/compatibility/v2/file_backed_block_device.h"
// clang-format on

namespace f2fs {

FileBackedBlockDevice::FileBackedBlockDevice(fbl::unique_fd fd, const uint64_t block_count,
                                             const uint32_t block_size)
    : fd_(std::move(fd)), block_count_(block_count), block_size_(block_size) {}

zx_status_t FileBackedBlockDevice::FifoTransaction(block_fifo_request_t* requests, size_t count) {
  std::lock_guard mutex_lock(mutex_);
  for (size_t i = 0; i < count; ++i) {
    switch (requests[i].opcode & BLOCKIO_OP_MASK) {
      case BLOCKIO_READ: {
        vmoid_t vmoid = requests[i].vmoid;
        zx::vmo& target_vmoid = vmos_.at(vmoid);
        uint8_t buffer[block_size_];
        memset(buffer, 0, block_size_);
        for (size_t j = 0; j < requests[i].length; ++j) {
          uint64_t offset = (requests[i].dev_offset + j) * block_size_;
          if (lseek(fd_.get(), offset, SEEK_SET) < 0) {
            FX_LOGS(ERROR) << "seek for read failed at " << offset << ". " << errno;
            return ZX_ERR_IO;
          }
          if (size_t ret = read(fd_.get(), buffer, block_size_); ret != block_size_) {
            FX_LOGS(ERROR) << "read failed at " << offset;
            return ZX_ERR_IO;
          }

          offset = (requests[i].vmo_offset + j) * block_size_;
          if (zx_status_t status = target_vmoid.write(buffer, offset, block_size_);
              status != ZX_OK) {
            FX_LOGS(ERROR) << "Write to buffer failed: offset=" << offset
                           << ", block_size_=" << block_size_;
            return status;
          }
        }
        break;
      }
      case BLOCKIO_WRITE: {
        vmoid_t vmoid = requests[i].vmoid;
        zx::vmo& target_vmoid = vmos_.at(vmoid);
        uint8_t buffer[block_size_];
        memset(buffer, 0, block_size_);
        for (size_t j = 0; j < requests[i].length; j++) {
          uint64_t offset = (requests[i].vmo_offset + j) * block_size_;
          if (zx_status_t status = target_vmoid.read(buffer, offset, block_size_);
              status != ZX_OK) {
            FX_LOGS(ERROR) << "Read from buffer failed: offset=" << offset
                           << ", block_size_=" << block_size_;
            return status;
          }

          offset = (requests[i].dev_offset + j) * block_size_;
          if (lseek(fd_.get(), offset, SEEK_SET) < 0) {
            FX_LOGS(ERROR) << "seek for write failed at " << offset << ". " << errno;
            return ZX_ERR_IO;
          }
          if (size_t ret = write(fd_.get(), buffer, block_size_); ret != block_size_) {
            FX_LOGS(ERROR) << "write failed at " << offset;
            return ZX_ERR_IO;
          }
        }
        break;
      }
      case BLOCKIO_FLUSH:
        continue;
      case BLOCKIO_CLOSE_VMO:
        vmos_.erase(requests[i].vmoid);
        break;
      case BLOCKIO_TRIM:
      // TODO: support trim using truncate
      default:
        return ZX_ERR_NOT_SUPPORTED;
    }
  }
  return ZX_OK;
}

zx_status_t FileBackedBlockDevice::BlockGetInfo(
    fuchsia_hardware_block::wire::BlockInfo* out_info) const {
  out_info->block_count = block_count_;
  out_info->block_size = block_size_;
  out_info->flags = block_info_flags_;
  out_info->max_transfer_size = max_transfer_size_;
  return ZX_OK;
}

zx_status_t FileBackedBlockDevice::BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out_vmoid) {
  zx::vmo xfer_vmo;
  zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &xfer_vmo);
  if (status != ZX_OK) {
    return status;
  }

  std::lock_guard mutex_lock(mutex_);
  // Find a free vmoid.
  vmoid_t vmoid = 1;
  for (const auto& [used_vmoid, vmo] : vmos_) {
    if (used_vmoid > vmoid) {
      break;
    }
    if (used_vmoid == std::numeric_limits<vmoid_t>::max()) {
      return ZX_ERR_NO_RESOURCES;
    }
    vmoid = used_vmoid + 1;
  }
  vmos_.insert(std::make_pair(vmoid, std::move(xfer_vmo)));
  *out_vmoid = storage::Vmoid(vmoid);
  return ZX_OK;
}

}  // namespace f2fs
