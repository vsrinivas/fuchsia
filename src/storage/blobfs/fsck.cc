// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <zircon/status.h>

#include <memory>

#include <blobfs/fsck.h>
#include <fs/journal/replay.h>
#include <fs/trace.h>

#include "blobfs-checker.h"
#include "blobfs.h"
#include "iterator/extent-iterator.h"

namespace blobfs {

zx_status_t Fsck(std::unique_ptr<block_client::BlockDevice> device, MountOptions* options) {
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
  if (options->writability == Writability::ReadOnlyDisk) {
    checker_options.repair = false;
  }
  BlobfsChecker checker(std::move(blobfs), checker_options);

  // Apply writeback and validate FVM data before walking the contents of the filesystem.
  status = checker.Initialize(options->journal);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to initialize filesystem; not checking internals\n");
    return status;
  }

  return checker.Check();
}

}  // namespace blobfs
