// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __Fuchsia__
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <storage/buffer/vmo_buffer.h>
#include <storage/operation/operation.h>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#endif  // __Fuchsia__

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <storage/buffer/block_buffer.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

#ifdef __Fuchsia__
zx_status_t CreateBcache(std::unique_ptr<block_client::BlockDevice> device, bool* out_readonly,
                         std::unique_ptr<f2fs::Bcache>* out) {
  fuchsia_hardware_block_BlockInfo info;
  if (zx_status_t status = device->BlockGetInfo(&info); status != ZX_OK) {
    FX_LOGS(ERROR) << "Coult not access device info: " << status;
    return status;
  }

  uint64_t device_size = info.block_size * info.block_count;

  if (device_size == 0) {
    FX_LOGS(ERROR) << "Invalid device size";
    return ZX_ERR_NO_RESOURCES;
  }
  uint64_t block_count = device_size / kBlockSize;

  // The maximum volume size of f2fs is 16TB
  if (block_count >= std::numeric_limits<uint32_t>::max()) {
    FX_LOGS(ERROR) << "Block count overflow";
    return ZX_ERR_OUT_OF_RANGE;
  }

  return f2fs::Bcache::Create(std::move(device), block_count, kBlockSize, out);
}

std::unique_ptr<block_client::BlockDevice> Bcache::Destroy(std::unique_ptr<Bcache> bcache) {
  {
    // Destroy the VmoBuffer before extracting the underlying device, as it needs
    // to de-register itself from the underlying block device to be terminated.
    __UNUSED auto unused = std::move(bcache->buffer_);
  }
  return std::move(bcache->owned_device_);
}
#endif  // __Fuchsia__

#ifdef __Fuchsia__
zx_status_t Bcache::Readblk(block_t bno, void* data) {
  TRACE_DURATION("f2fs", "Bcache::Readblk", "blk", bno);
  if (bno >= max_blocks_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  storage::Operation operation = {};
  operation.type = storage::OperationType::kRead;
  operation.vmo_offset = 0;
  operation.dev_offset = bno;
  operation.length = 1;
  zx_status_t status = RunOperation(operation, &buffer_);
  if (status != ZX_OK) {
    return status;
  }
  memcpy(data, buffer_.Data(0), BlockSize());
  return ZX_OK;
}
#else   // __Fuchsia__
zx_status_t Bcache::Readblk(block_t bno, void* data) {
  off_t off = static_cast<off_t>(bno) * kBlockSize;
  assert(off / kBlockSize == bno);  // Overflow
  if (lseek(fd_.get(), off, SEEK_SET) < 0) {
    FX_LOGS(ERROR) << "cannot seek to block " << bno;
    return ZX_ERR_IO;
  }
  if (read(fd_.get(), data, kBlockSize) != kBlockSize) {
    FX_LOGS(ERROR) << "cannot read block " << bno;
    return ZX_ERR_IO;
  }
  return ZX_OK;
}
#endif  // __Fuchsia__

#ifdef __Fuchsia__
zx_status_t Bcache::Writeblk(block_t bno, const void* data) {
  if (bno >= max_blocks_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  TRACE_DURATION("f2fs", "Bcache::Writeblk", "blk", bno);
  storage::Operation operation = {};
  operation.type = storage::OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = bno;
  operation.length = 1;
  memcpy(buffer_.Data(0), data, BlockSize());
  return RunOperation(operation, &buffer_);
}
#else   // __Fuchsia__
zx_status_t Bcache::Writeblk(block_t bno, const void* data) {
  off_t off = static_cast<off_t>(bno) * kBlockSize;
  assert(off / kBlockSize == bno);  // Overflow
  if (lseek(fd_.get(), off, SEEK_SET) < 0) {
    FX_LOGS(ERROR) << "cannot seek to block " << bno << ". " << errno;
    return ZX_ERR_IO;
  }
  ssize_t ret = write(fd_.get(), data, kBlockSize);
  if (ret != kBlockSize) {
    FX_LOGS(ERROR) << "cannot write block " << bno << " (" << ret << ")";
    return ZX_ERR_IO;
  }
  return ZX_OK;
}
#endif  // __Fuchsia__

zx_status_t Bcache::Trim(block_t start, block_t num) {
#ifdef __Fuchsia__
  if (!(info_.flags & fuchsia_hardware_block_FLAG_TRIM_SUPPORT)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  block_fifo_request_t request = {
      .opcode = BLOCKIO_TRIM,
      .vmoid = BLOCK_VMOID_INVALID,
      .length = static_cast<uint32_t>(BlockNumberToDevice(num)),
      .vmo_offset = 0,
      .dev_offset = BlockNumberToDevice(start),
  };

  return device()->FifoTransaction(&request, 1);
#else   // __Fuchsia__
  return ZX_OK;
#endif  // __Fuchsia__
}

#ifdef __Fuchsia__
zx_status_t Bcache::BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) {
  return device()->BlockAttachVmo(vmo, out);
}

zx_status_t Bcache::BlockDetachVmo(storage::Vmoid vmoid) {
  return device()->BlockDetachVmo(std::move(vmoid));
}

zx_status_t Bcache::Create(std::unique_ptr<block_client::BlockDevice> device, uint64_t max_blocks,
                           block_t block_size, std::unique_ptr<Bcache>* out) {
  zx_status_t status = Create(device.get(), max_blocks, block_size, out);
  if (status == ZX_OK) {
    (*out)->owned_device_ = std::move(device);
  }
  return status;
}

zx_status_t Bcache::Create(block_client::BlockDevice* device, uint64_t max_blocks,
                           block_t block_size, std::unique_ptr<Bcache>* out) {
  std::unique_ptr<Bcache> bcache(new Bcache(device, max_blocks, block_size));

  zx_status_t status = bcache->buffer_.Initialize(bcache.get(), 1, block_size, "scratch-block");
  if (status != ZX_OK) {
    return status;
  }

  status = bcache->VerifyDeviceInfo();
  if (status != ZX_OK) {
    return status;
  }

  *out = std::move(bcache);
  return ZX_OK;
}

#else   // __Fuchsia__
zx_status_t Bcache::Create(fbl::unique_fd fd, uint64_t max_blocks, std::unique_ptr<Bcache>* out) {
  uint64_t max_blocks_converted = max_blocks * kBlockSize / kDefaultSectorSize;
  out->reset(new Bcache(std::move(fd), max_blocks_converted));
  return ZX_OK;
}
#endif  // __Fuchsia__

uint64_t Bcache::DeviceBlockSize() const {
#ifdef __Fuchsia__
  return info_.block_size;
#else   // __Fuchsia__
  return kDefaultSectorSize;
#endif  // __Fuchsia__
}

#ifdef __Fuchsia__
Bcache::Bcache(block_client::BlockDevice* device, uint64_t max_blocks, block_t block_size)
    : max_blocks_(max_blocks), block_size_(block_size), device_(device) {}
#else   // __Fuchsia__
Bcache::Bcache(fbl::unique_fd fd, uint64_t max_blocks)
    : max_blocks_(max_blocks), fd_(std::move(fd)) {}
#endif  // __Fuchsia__

zx_status_t Bcache::VerifyDeviceInfo() {
#ifdef __Fuchsia__
  zx_status_t status = device_->BlockGetInfo(&info_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot get block device information: " << status;
    return status;
  }

  if (BlockSize() % info_.block_size != 0) {
    FX_LOGS(WARNING) << "f2fs block size cannot be multiple of underlying block size: "
                     << info_.block_size;
    return ZX_ERR_BAD_STATE;
  }
#endif  // __Fuchsia__
  return ZX_OK;
}

void Bcache::Pause() {
#ifdef __Fuchsia__
  mutex_.lock();
#endif  // __Fuchsia__
}

void Bcache::Resume() {
#ifdef __Fuchsia__
  mutex_.unlock();
#endif  // __Fuchsia__
}

}  // namespace f2fs
