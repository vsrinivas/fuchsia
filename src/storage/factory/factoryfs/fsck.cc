// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/factory/factoryfs/fsck.h"

#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <memory>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/storage/factory/factoryfs/factoryfs.h"
#include "src/storage/factory/factoryfs/superblock.h"

namespace factoryfs {

zx_status_t Fsck(std::unique_ptr<block_client::BlockDevice> device, MountOptions* options) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  if (zx_status_t status = loop.StartThread(); status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot initialize dispatch loop";
    return status;
  }

  auto vfs = std::make_unique<fs::ManagedVfs>(loop.dispatcher());
  auto fs_or = Factoryfs::Create(loop.dispatcher(), std::move(device), options, vfs.get());
  if (fs_or.is_error()) {
    FX_LOGS(ERROR) << "Cannot create filesystem instance for checking" << fs_or.status_string();
    return fs_or.error_value();
  }

  // TODO(manalib) add more functionality for checking directory entries.
  auto superblock = fs_or->Info();
  if (zx_status_t status = CheckSuperblock(&superblock); status != ZX_OK) {
    FX_LOGS(ERROR) << "Check Superblock failure";
    return status;
  }

  FX_LOGS(INFO) << "Filesystem checksum success!!";
  return ZX_OK;
}

}  // namespace factoryfs
