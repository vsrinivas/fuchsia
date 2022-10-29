// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/factory/factoryfs/format.h"

#include <lib/syslog/cpp/macros.h>

#include <storage/buffer/owned_vmoid.h>

#include "src/lib/storage/block_client/cpp/block_device.h"
#include "src/storage/factory/factoryfs/factoryfs.h"
#include "src/storage/factory/factoryfs/mkfs.h"
#include "src/storage/factory/factoryfs/superblock.h"

namespace factoryfs {
namespace {

// Take the contents of the filesystem, generated in-memory, and transfer
// them to the underlying device.
zx_status_t WriteFilesystemToDisk(block_client::BlockDevice* device, const Superblock& superblock,
                                  uint32_t block_size) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kFactoryfsBlockSize, 0, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  storage::OwnedVmoid vmoid;
  status = device->BlockAttachVmo(vmo, &vmoid.GetReference(device));
  if (status != ZX_OK) {
    return status;
  }

  // Write the superblock.
  status = vmo.write(&superblock, 0, kFactoryfsBlockSize);
  if (status != ZX_OK) {
    FX_LOGS(INFO) << "\nfactoryfs: error writing superblock block";
    return status;
  }

  fuchsia_hardware_block::wire::BlockInfo block_info;
  status = device->BlockGetInfo(&block_info);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot acquire block info: " << status;
    return status;
  }
  block_fifo_request_t request = {};
  // write superblock.
  request.opcode = BLOCKIO_WRITE;
  request.vmoid = vmoid.get();
  request.length = FsToDeviceBlocks(1, block_info.block_size);
  request.vmo_offset = FsToDeviceBlocks(0, block_info.block_size);
  request.dev_offset = FsToDeviceBlocks(0, block_info.block_size);

  return device->FifoTransaction(&request, 1);
}

}  // namespace

zx_status_t FormatFilesystem(block_client::BlockDevice* device) {
  zx_status_t status;
  fuchsia_hardware_block::wire::BlockInfo block_info = {};
  status = device->BlockGetInfo(&block_info);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "cannot acquire block info: " << status;
    return status;
  }

  if (block_info.flags & BLOCK_FLAG_READONLY) {
    FX_LOGS(ERROR) << "cannot format read-only device";
    return ZX_ERR_ACCESS_DENIED;
  }
  if (block_info.block_size == 0 || block_info.block_count == 0) {
    return ZX_ERR_NO_SPACE;
  }
  if (kFactoryfsBlockSize % block_info.block_size != 0) {
    return ZX_ERR_IO_INVALID;
  }

  uint64_t blocks = (block_info.block_size * block_info.block_count) / kFactoryfsBlockSize;
  Superblock superblock;
  InitializeSuperblock(blocks, &superblock);

  status = CheckSuperblock(&superblock);
  ZX_DEBUG_ASSERT(status == ZX_OK);

  status = WriteFilesystemToDisk(device, superblock, block_info.block_size);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to write to disk: " << status;
    return status;
  }

  FX_LOGS(DEBUG) << "mkfs success";
  return ZX_OK;
}

}  // namespace factoryfs
