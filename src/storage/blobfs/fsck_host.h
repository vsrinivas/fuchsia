// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functionality for checking the consistency of
// Blobfs.

#ifndef SRC_STORAGE_BLOBFS_FSCK_HOST_H_
#define SRC_STORAGE_BLOBFS_FSCK_HOST_H_

#include <memory>

#include "src/storage/blobfs/host.h"

namespace blobfs {

zx_status_t Fsck(Blobfs* blobfs);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_FSCK_HOST_H_
