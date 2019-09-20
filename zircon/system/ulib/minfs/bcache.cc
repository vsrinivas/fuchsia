// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <minfs/bcache.h>

#include <assert.h>
#include <fuchsia/device/c/fidl.h>
#include <lib/fdio/directory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <utility>

#include <block-client/cpp/remote-block-device.h>
#include <fbl/alloc_checker.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fs/buffer/block_buffer.h>
#include <fs/buffer/vmo_buffer.h>
#include <fs/operation/operation.h>
#include <fs/trace.h>
#include <minfs/format.h>

#include "minfs-private.h"

namespace minfs {

zx_status_t Bcache::Readblk(blk_t bno, void* data) {
  fs::Operation operation = {};
  operation.type = fs::OperationType::kRead;
  operation.vmo_offset = 0;
  operation.dev_offset = bno;
  operation.length = 1;
  zx_status_t status = RunOperation(operation, &buffer_);
  if (status != ZX_OK) {
    return status;
  }
  memcpy(data, buffer_.Data(0), kMinfsBlockSize);
  return ZX_OK;
}

zx_status_t Bcache::Writeblk(blk_t bno, const void* data) {
  fs::Operation operation = {};
  operation.type = fs::OperationType::kWrite;
  operation.vmo_offset = 0;
  operation.dev_offset = bno;
  operation.length = 1;
  memcpy(buffer_.Data(0), data, kMinfsBlockSize);
  return RunOperation(operation, &buffer_);
}

zx_status_t Bcache::AttachVmo(const zx::vmo& vmo, vmoid_t* out) {
  fuchsia_hardware_block_VmoID vmoid;
  zx_status_t status = device()->BlockAttachVmo(vmo, &vmoid);
  *out = vmoid.id;
  return status;
}

zx_status_t Bcache::DetachVmo(vmoid_t vmoid) {
  block_fifo_request_t request = {};
  request.group = BlockGroupID();
  request.vmoid = vmoid;
  request.opcode = BLOCKIO_CLOSE_VMO;
  return Transaction(&request, 1);
}

int Bcache::Sync() {
  fs::WriteTxn sync_txn(this);
  sync_txn.EnqueueFlush();
  return sync_txn.Transact();
}

zx_status_t FdToBlockDevice(fbl::unique_fd& fd,
                            std::unique_ptr<block_client::BlockDevice>* out) {
  zx::channel channel, server;
  zx_status_t status = zx::channel::create(0, &channel, &server);
  if (status != ZX_OK) {
    return status;
  }
  fzl::UnownedFdioCaller caller(fd.get());
  uint32_t flags = ZX_FS_FLAG_CLONE_SAME_RIGHTS;
  status = fuchsia_io_NodeClone(caller.borrow_channel(), flags, server.release());
  if (status != ZX_OK) {
    return status;
  }
  std::unique_ptr<block_client::RemoteBlockDevice> device;
  status = block_client::RemoteBlockDevice::Create(std::move(channel), &device);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: cannot create block device: %d\n", status);
    return status;
  }

  *out = std::move(device);
  return ZX_OK;
}

// Static.
zx_status_t Bcache::Create(std::unique_ptr<block_client::BlockDevice> device,
                           uint32_t max_blocks, std::unique_ptr<Bcache>* out) {
  std::unique_ptr<Bcache> bcache(new Bcache(std::move(device), max_blocks));

  zx_status_t status = bcache->buffer_.Initialize(bcache.get(), 1, kMinfsBlockSize,
                                                  "scratch-block");
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

groupid_t Bcache::BlockGroupID() {
  thread_local groupid_t group = next_group_.fetch_add(1);
  ZX_ASSERT_MSG(group < MAX_TXN_GROUP_COUNT, "Too many threads accessing block device");
  return group;
}

uint32_t Bcache::DeviceBlockSize() const { return info_.block_size; }

zx_status_t Bcache::RunOperation(const fs::Operation& operation, fs::BlockBuffer* buffer) {
  if (operation.type != fs::OperationType::kWrite && operation.type != fs::OperationType::kRead) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  block_fifo_request_t request;
  request.group = BlockGroupID();
  request.vmoid = buffer->vmoid();
  request.opcode = operation.type == fs::OperationType::kWrite ? BLOCKIO_WRITE : BLOCKIO_READ;
  request.vmo_offset = BlockNumberToDevice(operation.vmo_offset);
  request.dev_offset = BlockNumberToDevice(operation.dev_offset);
  uint64_t length = BlockNumberToDevice(operation.length);
  ZX_ASSERT_MSG(length < UINT32_MAX, "Request size too large");
  request.length = static_cast<uint32_t>(length);

  return device_->FifoTransaction(&request, 1);
}

Bcache::Bcache(std::unique_ptr<block_client::BlockDevice> device, uint32_t max_blocks)
    : max_blocks_(max_blocks), device_(std::move(device)) {}

zx_status_t Bcache::VerifyDeviceInfo() {
  zx_status_t status = device_->BlockGetInfo(&info_);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: cannot get block device information: %d\n", status);
    return status;
  }

  if (kMinfsBlockSize % info_.block_size != 0) {
    FS_TRACE_ERROR("minfs: minfs Block size not multiple of underlying block size: %d\n",
                   info_.block_size);
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

}  // namespace minfs
