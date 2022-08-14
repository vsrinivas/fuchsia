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
  // Destroy the VmoBuffer before extracting the underlying device, as it needs
  // to de-register itself from the underlying block device to be terminated.
  bcache->DestroyVmoBuffer();
  return std::move(bcache->owned_device_);
}

Bcache::Bcache(block_client::BlockDevice* device, uint64_t max_blocks, block_t block_size)
    : max_blocks_(max_blocks), block_size_(block_size), device_(device) {}

zx_status_t Bcache::BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) {
  return GetDevice()->BlockAttachVmo(vmo, out);
}

zx_status_t Bcache::BlockDetachVmo(storage::Vmoid vmoid) {
  return GetDevice()->BlockDetachVmo(std::move(vmoid));
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

  zx_status_t status = bcache->CreateVmoBuffer();
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
Bcache::Bcache(fbl::unique_fd fd, uint64_t max_blocks)
    : max_blocks_(max_blocks), fd_(std::move(fd)), buffer_(1, kBlockSize) {}

zx_status_t Bcache::Create(fbl::unique_fd fd, uint64_t max_blocks, std::unique_ptr<Bcache>* out) {
  uint64_t max_blocks_converted = max_blocks * kBlockSize / kDefaultSectorSize;
  std::unique_ptr<Bcache> bcache(new Bcache(std::move(fd), max_blocks_converted));
  *out = std::move(bcache);
  return ZX_OK;
}
#endif  // __Fuchsia__

zx_status_t Bcache::Readblk(block_t bno, void* data) {
  if (bno >= max_blocks_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
#ifdef __Fuchsia__
  TRACE_DURATION("f2fs", "Bcache::Readblk", "blk", bno);
#endif
  storage::Operation operation = {};
  operation.type = storage::OperationType::kRead;
  operation.vmo_offset = 0;
  operation.dev_offset = bno;
  operation.length = 1;
  std::lock_guard lock(buffer_mutex_);
  zx_status_t status = RunOperation(operation, &buffer_);
  if (status != ZX_OK) {
    return status;
  }
  std::memcpy(data, buffer_.Data(0), BlockSize());
  return ZX_OK;
}

zx_status_t Bcache::Writeblk(block_t bno, const void* data) {
  if (bno >= max_blocks_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
#ifdef __Fuchsia__
  TRACE_DURATION("f2fs", "Bcache::Writeblk", "blk", bno);
#endif
  storage::Operation operation = {};
  operation.type = storage::OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = bno;
  operation.length = 1;
  std::lock_guard lock(buffer_mutex_);
  std::memcpy(buffer_.Data(0), data, BlockSize());
  return RunOperation(operation, &buffer_);
}

#ifdef __Fuchsia__
zx_status_t Bcache::RunRequests(const std::vector<storage::BufferedOperation>& operations) {
  std::shared_lock lock(mutex_);
  return DeviceTransactionHandler::RunRequests(operations);
}

zx_status_t Bcache::VerifyDeviceInfo() {
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
  return ZX_OK;
}
#else   // __Fuchsia__
zx_status_t Bcache::RunOperation(const storage::Operation& operation,
                                 storage::BlockBuffer* buffer) {
  return TransactionHandler::RunOperation(operation, buffer);
}

zx_status_t Bcache::RunRequests(const std::vector<storage::BufferedOperation>& operations) {
  std::shared_lock lock(mutex_);
  for (auto& operation : operations) {
    const auto& op = operation.op;
    off_t off = static_cast<off_t>(op.dev_offset) * BlockSize();

    if (lseek(fd_.get(), off, SEEK_SET) < 0) {
      FX_LOGS(ERROR) << "seek failed at " << op.dev_offset << ". " << errno;
      return ZX_ERR_IO;
    }

    size_t length = op.length * BlockSize();
    size_t buffer_offset = op.vmo_offset * BlockSize();
    switch (op.type) {
      case storage::OperationType::kRead:
        if (size_t ret =
                read(fd_.get(), static_cast<uint8_t*>(operation.data) + buffer_offset, length);
            ret != length) {
          FX_LOGS(ERROR) << "read failed at " << op.dev_offset;
          return ZX_ERR_IO;
        }
        break;
      case storage::OperationType::kWrite:
        if (size_t ret =
                write(fd_.get(), static_cast<uint8_t*>(operation.data) + buffer_offset, length);
            ret != length) {
          FX_LOGS(ERROR) << "write failed at " << op.dev_offset << " (" << ret << ")";
          return ZX_ERR_IO;
        }
        break;
      case storage::OperationType::kTrim:
        // TODO : zeroing
        break;
      default:
        ZX_DEBUG_ASSERT_MSG(false, "Unsupported operation");
    }
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

  return GetDevice()->FifoTransaction(&request, 1);
#else   // __Fuchsia__
  return ZX_OK;
#endif  // __Fuchsia__
}

}  // namespace f2fs
