// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/fsck.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <memory>

#include "src/lib/storage/vfs/cpp/paged_vfs.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/blobfs_checker.h"
#include "zircon/errors.h"

namespace blobfs {

// To run Fsck we mount Blobfs on the given BlockDevice. This requires a dispatcher. This function
// may be called in different contexts where there might not be an easily known dispatcher or none
// set up.
//
// To make this uniform from the caller's perspective, Blobfs is run on a new thread with a
// dedicated dispatcher.
zx_status_t Fsck(std::unique_ptr<block_client::BlockDevice> device, const MountOptions& options) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  if (zx_status_t status = loop.StartThread(); status != ZX_OK) {
    FX_LOGS(ERROR) << "Cannot initialize dispatch loop: " << zx_status_get_string(status);
    return status;
  }

  auto vfs = std::make_unique<fs::PagedVfs>(loop.dispatcher());
  if (auto status = vfs->Init(); status.is_error())
    return status.error_value();

  auto defer = fit::defer([&] { vfs->TearDown(); });

  auto blobfs_or = Blobfs::Create(loop.dispatcher(), std::move(device), vfs.get(), options);
  if (blobfs_or.is_error()) {
    FX_LOGS(ERROR) << "Cannot create filesystem for checking: " << blobfs_or.status_string();
    return blobfs_or.status_value();
  }
  Blobfs* blobfs = blobfs_or.value().get();

  sync_completion_t completion;
  zx_status_t result = ZX_OK;

  async::PostTask(
      loop.dispatcher(), [dispatcher = loop.dispatcher(), blobfs, &result, &completion]() mutable {
        BlobfsChecker::Options checker_options;
        if (blobfs->writability() == Writability::ReadOnlyDisk) {
          checker_options.repair = false;
        }
        result = BlobfsChecker(blobfs, checker_options).Check() ? ZX_OK : ZX_ERR_IO_DATA_INTEGRITY;
        sync_completion_signal(&completion);
      });

  sync_completion_wait(&completion, ZX_TIME_INFINITE);

  return result;
}

}  // namespace blobfs
