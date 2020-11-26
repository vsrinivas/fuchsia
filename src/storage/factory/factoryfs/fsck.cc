// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/factory/factoryfs/fsck.h"

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <memory>

#include "src/storage/factory/factoryfs/factoryfs.h"
#include "src/storage/factory/factoryfs/superblock.h"

namespace factoryfs {

zx_status_t Fsck(std::unique_ptr<block_client::BlockDevice> device, MountOptions* options) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx_status_t status = loop.StartThread();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot initialize dispatch loop";
    return status;
  }

  std::unique_ptr<Factoryfs> factoryfs;
  status = Factoryfs::Create(loop.dispatcher(), std::move(device), options, &factoryfs);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot create filesystem instance for checking";
    return status;
  }
  // TODO(manalib) add more functionality for checking directory entries.
  auto superblock = factoryfs->Info();
  status = CheckSuperblock(&superblock);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Check Superblock failure";
    return status;
  }

  FX_LOGS(INFO) << "Filesystem checksum success!!";
  return ZX_OK;
}

}  // namespace factoryfs
