// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/fsck.h"

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <memory>

#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/blobfs_checker.h"
#include "src/storage/blobfs/iterator/extent_iterator.h"
#include "zircon/errors.h"

namespace blobfs {

zx_status_t Fsck(std::unique_ptr<block_client::BlockDevice> device, const MountOptions& options) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  if (zx_status_t status = loop.StartThread(); status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot initialize dispatch loop: " << zx_status_get_string(status);
    return status;
  }

  auto blobfs_or = Blobfs::Create(loop.dispatcher(), std::move(device), nullptr, options);
  if (blobfs_or.is_error()) {
    FX_LOGS(ERROR) << "Cannot create filesystem for checking: " << blobfs_or.status_string();
    return blobfs_or.status_value();
  }

  BlobfsChecker::Options checker_options;
  if (blobfs_or->writability() == Writability::ReadOnlyDisk) {
    checker_options.repair = false;
  }
  return BlobfsChecker(std::move(blobfs_or.value()), checker_options).Check()
             ? ZX_OK
             : ZX_ERR_IO_DATA_INTEGRITY;
}

}  // namespace blobfs
