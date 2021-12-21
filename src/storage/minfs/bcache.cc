// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/bcache.h"

#include <assert.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fuchsia/device/c/fidl.h>
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

#include "src/storage/minfs/format.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {

std::unique_ptr<block_client::BlockDevice> Bcache::Destroy(std::unique_ptr<Bcache> bcache) {
  {
    // Destroy the VmoBuffer before extracting the underlying device, as it needs
    // to de-register itself from the underlying block device to be terminated.
    __UNUSED auto unused = std::move(bcache->buffer_);
  }
  return std::move(bcache->owned_device_);
}

zx::status<> Bcache::Readblk(blk_t bno, void* data) {
  TRACE_DURATION("minfs", "Bcache::Readblk", "blk", bno);
  storage::Operation operation = {};
  operation.type = storage::OperationType::kRead;
  operation.vmo_offset = 0;
  operation.dev_offset = bno;
  operation.length = 1;
  zx_status_t status = RunOperation(operation, &buffer_);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  memcpy(data, buffer_.Data(0), kMinfsBlockSize);
  return zx::ok();
}

zx::status<> Bcache::Writeblk(blk_t bno, const void* data) {
  TRACE_DURATION("minfs", "Bcache::Writeblk", "blk", bno);
  storage::Operation operation = {};
  operation.type = storage::OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = bno;
  operation.length = 1;
  memcpy(buffer_.Data(0), data, kMinfsBlockSize);
  return zx::make_status(RunOperation(operation, &buffer_));
}

zx_status_t Bcache::BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) {
  return device()->BlockAttachVmo(vmo, out);
}

zx_status_t Bcache::BlockDetachVmo(storage::Vmoid vmoid) {
  return device()->BlockDetachVmo(std::move(vmoid));
}

zx::status<> Bcache::Sync() {
  block_fifo_request_t request = {};
  request.opcode = BLOCKIO_FLUSH;
  return zx::make_status(device_->FifoTransaction(&request, 1));
}

zx::status<std::unique_ptr<block_client::BlockDevice>> FdToBlockDevice(fbl::unique_fd& fd) {
  zx::channel channel, server;
  zx_status_t status = zx::channel::create(0, &channel, &server);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  fdio_cpp::UnownedFdioCaller caller(fd.get());
  status = fidl::WireCall<fuchsia_io::Node>(zx::unowned_channel(caller.borrow_channel()))
               ->Clone(fuchsia_io::wire::kCloneFlagSameRights, std::move(server))
               .status();
  if (status != ZX_OK) {
    return zx::error(status);
  }
  std::unique_ptr<block_client::RemoteBlockDevice> device;
  status = block_client::RemoteBlockDevice::Create(std::move(channel), &device);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot create block device: " << status;
    return zx::error(status);
  }

  return zx::ok(std::move(device));
}

zx::status<std::unique_ptr<Bcache>> Bcache::Create(
    std::unique_ptr<block_client::BlockDevice> device, uint32_t max_blocks) {
  auto bcache_or = Create(device.get(), max_blocks);
  if (bcache_or.is_ok()) {
    bcache_or->owned_device_ = std::move(device);
  }
  return bcache_or;
}

zx::status<std::unique_ptr<Bcache>> Bcache::Create(block_client::BlockDevice* device,
                                                   uint32_t max_blocks) {
  std::unique_ptr<Bcache> bcache(new Bcache(device, max_blocks));

  if (zx_status_t status =
          bcache->buffer_.Initialize(bcache.get(), 1, kMinfsBlockSize, "scratch-block");
      status != ZX_OK) {
    return zx::error(status);
  }

  if (auto status = bcache->VerifyDeviceInfo(); status.is_error()) {
    return status.take_error();
  }

  return zx::ok(std::move(bcache));
}

uint32_t Bcache::DeviceBlockSize() const { return info_.block_size; }

Bcache::Bcache(block_client::BlockDevice* device, uint32_t max_blocks)
    : max_blocks_(max_blocks), device_(device) {}

zx::status<> Bcache::VerifyDeviceInfo() {
  zx_status_t status = device_->BlockGetInfo(&info_);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot get block device information: " << status;
    return zx::error(status);
  }

  if (kMinfsBlockSize % info_.block_size != 0) {
    FX_LOGS(ERROR) << "minfs Block size not multiple of underlying block size: "
                   << info_.block_size;
    return zx::error(ZX_ERR_BAD_STATE);
  }
  return zx::ok();
}

void Bcache::Pause() { mutex_.lock(); }

void Bcache::Resume() { mutex_.unlock(); }

}  // namespace minfs
