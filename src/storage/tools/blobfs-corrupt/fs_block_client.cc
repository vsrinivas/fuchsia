// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs_block_client.h"

#include "src/storage/blobfs/format.h"

using block_client::BlockDevice;

zx_status_t FsBlockClient::Create(std::unique_ptr<BlockDevice> device,
                                  std::unique_ptr<FsBlockClient>* out) {
  fuchsia_hardware_block::wire::BlockInfo block_info;
  zx_status_t status = device->BlockGetInfo(&block_info);
  if (status != ZX_OK) {
    return status;
  }

  zx::vmo vmo;
  status = zx::vmo::create(blobfs::kBlobfsBlockSize, 0, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  storage::Vmoid vmoid;
  status = device->BlockAttachVmo(vmo, &vmoid);
  if (status != ZX_OK) {
    return status;
  }

  out->reset(new FsBlockClient(std::move(device), block_info, std::move(vmo), std::move(vmoid)));
  return ZX_OK;
}

uint64_t FsBlockClient::BlockCount() const {
  return block_info_.block_count / device_blocks_per_blobfs_block();
}

zx_status_t FsBlockClient::ReadBlock(uint64_t block, void* data) {
  block_fifo_request_t request = {};
  request.opcode = BLOCKIO_READ;
  request.vmoid = vmoid_.get();
  request.length = static_cast<uint32_t>(fs_block_to_device_block(1));
  request.vmo_offset = 0;
  request.dev_offset = fs_block_to_device_block(block);

  zx_status_t status = device_->FifoTransaction(&request, 1);
  if (status != ZX_OK) {
    return status;
  }

  return vmo_.read(data, 0, blobfs::kBlobfsBlockSize);
}

zx_status_t FsBlockClient::WriteBlock(uint64_t block, const void* data) {
  zx_status_t status = vmo_.write(data, 0, blobfs::kBlobfsBlockSize);
  if (status != ZX_OK) {
    return status;
  }

  block_fifo_request_t request = {};
  request.opcode = BLOCKIO_WRITE;
  request.vmoid = vmoid_.get();
  request.length = static_cast<uint32_t>(fs_block_to_device_block(1));
  request.vmo_offset = 0;
  request.dev_offset = fs_block_to_device_block(block);

  return device_->FifoTransaction(&request, 1);
}

FsBlockClient::FsBlockClient(std::unique_ptr<BlockDevice> device,
                             fuchsia_hardware_block::wire::BlockInfo block_info, zx::vmo vmo,
                             storage::Vmoid vmoid)
    : device_(std::move(device)),
      block_info_(block_info),
      vmo_(std::move(vmo)),
      vmoid_(std::move(vmoid)) {}

FsBlockClient::~FsBlockClient() {
  zx_status_t status = device_->BlockDetachVmo(std::move(vmoid_));
  ZX_DEBUG_ASSERT(status == ZX_OK);
}

uint64_t FsBlockClient::device_blocks_per_blobfs_block() const {
  return blobfs::kBlobfsBlockSize / block_info_.block_size;
}

uint64_t FsBlockClient::fs_block_to_device_block(uint64_t block) const {
  return block * device_blocks_per_blobfs_block();
}
