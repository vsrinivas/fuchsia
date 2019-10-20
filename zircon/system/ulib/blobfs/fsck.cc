// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <zircon/status.h>

#include <blobfs/fsck.h>
#include <fs/journal/replay.h>
#include <fs/trace.h>

#include "blobfs-checker.h"
#include "blobfs.h"
#include "iterator/extent-iterator.h"

namespace blobfs {

zx_status_t Fsck(std::unique_ptr<block_client::BlockDevice> device, blobfs::MountOptions* options) {
  std::unique_ptr<blobfs::Blobfs> blobfs;
  zx_status_t status = blobfs::Blobfs::Create(std::move(device), options, &blobfs);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Cannot create filesystem for checking\n");
    return status;
  }
  BlobfsChecker checker(std::move(blobfs));

  // Apply writeback and validate FVM data before walking the contents of the filesystem.
  status = checker.Initialize(options->journal);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to initialize filesystem; not checking internals\n");
    return status;
  }

  checker.TraverseInodeBitmap();
  checker.TraverseBlockBitmap();
  return checker.CheckAllocatedCounts();
}

}  // namespace blobfs
