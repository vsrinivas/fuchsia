// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/fsck_host.h"

#include <memory>

#include "src/storage/blobfs/blobfs_checker.h"
#include "zircon/errors.h"

namespace blobfs {

zx_status_t Fsck(Blobfs* blobfs) {
  return BlobfsChecker(blobfs).Check() ? ZX_OK : ZX_ERR_IO_DATA_INTEGRITY;
}

}  // namespace blobfs
