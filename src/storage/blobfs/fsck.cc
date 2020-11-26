// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <zircon/status.h>

#include <memory>

#include <blobfs/fsck.h>
#include <fs/trace.h>

#include "src/storage/blobfs/blobfs-checker.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/iterator/extent-iterator.h"

namespace blobfs {

zx_status_t Fsck(std::unique_ptr<block_client::BlockDevice> device, const MountOptions& options) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx_status_t status = loop.StartThread();
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot initialize dispatch loop\n");
    return status;
  }

  std::unique_ptr<Blobfs> blobfs;
  status = Blobfs::Create(loop.dispatcher(), std::move(device), options, zx::resource(), &blobfs);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot create filesystem for checking\n");
    return status;
  }
  BlobfsChecker::Options checker_options;
  if (blobfs->writability() == Writability::ReadOnlyDisk) {
    checker_options.repair = false;
  }
  return BlobfsChecker(std::move(blobfs), checker_options).Check();
}

}  // namespace blobfs
