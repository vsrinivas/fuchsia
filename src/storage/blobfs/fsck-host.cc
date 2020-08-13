// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <blobfs/host/fsck.h>
#include <fs/trace.h>

#include "blobfs-checker.h"

namespace blobfs {

zx_status_t Fsck(std::unique_ptr<Blobfs> blob, bool apply_journal) {
  BlobfsChecker checker(std::move(blob));

  // Apply writeback and validate FVM data before walking the contents of the filesystem.
  zx_status_t status = checker.Initialize(apply_journal);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("blobfs: Failed to initialize filesystem; not checking internals\n");
    return status;
  }

  return checker.Check();
}

}  // namespace blobfs
