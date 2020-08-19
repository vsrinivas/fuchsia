// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/factory/factoryfs/factoryfs.h"

#include <getopt.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/status.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include <block-client/cpp/remote-block-device.h>
#include <fs/managed_vfs.h>
#include <fs/pseudo_dir.h>
#include <fs/vnode.h>
#include <storage/buffer/owned_vmoid.h>

#include "src/storage/factory/factoryfs/directory.h"
#include "src/storage/factory/factoryfs/format.h"
#include "src/storage/factory/factoryfs/superblock.h"

namespace factoryfs {

uint32_t FsToDeviceBlocks(uint32_t fs_block, uint32_t disk_block_size) {
  return fs_block * (kFactoryfsBlockSize / disk_block_size);
};

zx_status_t Factoryfs::OpenRootNode(fbl::RefPtr<fs::Vnode>* out) {
  fbl::RefPtr<Directory> vn = fbl::AdoptRef(new Directory(this));
  auto validated_options = vn->ValidateOptions(fs::VnodeConnectionOptions());
  if (validated_options.is_error()) {
    return validated_options.error();
  }
  zx_status_t status = vn->Open(validated_options.value(), nullptr);
  if (status != ZX_OK) {
    return status;
  }

  *out = std::move(vn);
  return ZX_OK;
}

Factoryfs::Factoryfs(std::unique_ptr<BlockDevice> device, const Superblock* superblock)
    : block_device_(std::move(device)), superblock_(*superblock) {}

std::unique_ptr<BlockDevice> Factoryfs::Reset() {
  if (!block_device_) {
    return nullptr;
  }
  // TODO(manalib) Shutdown all internal connections to factoryfs,
  // by iterating over open_vnodes
  return std::move(block_device_);
}

zx_status_t Factoryfs::Create(async_dispatcher_t* dispatcher, std::unique_ptr<BlockDevice> device,
                              MountOptions* options, std::unique_ptr<Factoryfs>* out) {
  TRACE_DURATION("factoryfs", "Factoryfs::Create");
  Superblock superblock;
  zx_status_t status = device->ReadBlock(0, kFactoryfsBlockSize, &superblock);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: could not read info block\n");
    return status;
  }

  fuchsia_hardware_block_BlockInfo block_info;
  status = device->BlockGetInfo(&block_info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: cannot acquire block info: %d\n", status);
    return status;
  }
  // TODO(manalib).
  // Both generic fsck as well as generic mount open the device in read-write mode.
  // Hence we cannot return an error here. Simply flagging this inconsistency for now.
  if ((block_info.flags & BLOCK_FLAG_READONLY) == 0) {
    FS_TRACE_ERROR("factoryfs: Factory partition should only be mounting as read-only.\n");
    // return ZX_ERR_IO;
  }
  if (kFactoryfsBlockSize % block_info.block_size != 0) {
    FS_TRACE_ERROR("factoryfs: Factoryfs block size (%u) not divisible by device block size (%u)\n",
                   kFactoryfsBlockSize, block_info.block_size);
    return ZX_ERR_IO;
  }

  // Perform superblock validations.
  status = CheckSuperblock(&superblock);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("factoryfs: Check Superblock failure\n");
    return status;
  }

  auto fs = std::unique_ptr<Factoryfs>(new Factoryfs(std::move(device), &superblock));
  fs->block_info_ = std::move(block_info);

  *out = std::move(fs);
  return ZX_OK;
}

Factoryfs::~Factoryfs() { Reset(); }

zx_status_t Factoryfs::GetFsId(zx::event* out_fs_id) const {
  ZX_DEBUG_ASSERT(fs_id_.is_valid());
  return fs_id_.duplicate(ZX_RIGHTS_BASIC, out_fs_id);
}

}  // namespace factoryfs
