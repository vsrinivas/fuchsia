// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <utility>

#include <block-client/cpp/remote-block-device.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <storage/buffer/block_buffer.h>
#include <storage/buffer/vmo_buffer.h>
#include <storage/operation/operation.h>

#include "f2fs.h"

namespace f2fs {

zx_status_t CreateBcache(std::unique_ptr<block_client::BlockDevice> device, bool* out_readonly,
                         std::unique_ptr<f2fs::Bcache>* out) {
  fuchsia_hardware_block_BlockInfo info;
  zx_status_t status = device->BlockGetInfo(&info);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Coult not access device info: " << status;
    return status;
  }

  uint64_t device_size = info.block_size * info.block_count;

  if (device_size == 0) {
    FX_LOGS(ERROR) << "Invalid device size";
    return status;
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

zx_status_t Bcache::Readblk(block_t bno, void* data) {
  TRACE_DURATION("f2fs", "Bcache::Readblk", "blk", bno);
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

zx_status_t Bcache::Writeblk(block_t bno, const void* data) {
  TRACE_DURATION("f2fs", "Bcache::Writeblk", "blk", bno);
  storage::Operation operation = {};
  operation.type = storage::OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = bno;
  operation.length = 1;
  memcpy(buffer_.Data(0), data, BlockSize());
  return RunOperation(operation, &buffer_);
}

zx_status_t Bcache::Trim(block_t start, block_t num) {
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
}

zx_status_t Bcache::BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) {
  return device()->BlockAttachVmo(vmo, out);
}

zx_status_t Bcache::BlockDetachVmo(storage::Vmoid vmoid) {
  return device()->BlockDetachVmo(std::move(vmoid));
}

zx_status_t Bcache::Create(std::unique_ptr<block_client::BlockDevice> device, uint64_t max_blocks,
                           uint64_t block_size, std::unique_ptr<Bcache>* out) {
  zx_status_t status = Create(device.get(), max_blocks, block_size, out);
  if (status == ZX_OK) {
    (*out)->owned_device_ = std::move(device);
  }
  return status;
}

zx_status_t Bcache::Create(block_client::BlockDevice* device, uint64_t max_blocks,
                           uint64_t block_size, std::unique_ptr<Bcache>* out) {
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

uint64_t Bcache::DeviceBlockSize() const { return info_.block_size; }

Bcache::Bcache(block_client::BlockDevice* device, uint64_t max_blocks, uint64_t block_size)
    : max_blocks_(max_blocks), block_size_(block_size), device_(device) {}

zx_status_t Bcache::VerifyDeviceInfo() {
  zx_status_t status = device_->BlockGetInfo(&info_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot get block device information: " << status;
    return status;
  }

  if (BlockSize() % info_.block_size != 0) {
    FX_LOGS(ERROR) << "f2fs block size cannot be multiple of underlying block size: "
                   << info_.block_size;
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

void Bcache::Pause() { mutex_.lock(); }

void Bcache::Resume() { mutex_.unlock(); }

}  // namespace f2fs
