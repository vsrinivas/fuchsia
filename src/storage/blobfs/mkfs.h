// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_MKFS_H_
#define SRC_STORAGE_BLOBFS_MKFS_H_

#include "src/lib/storage/block_client/cpp/block_device.h"
#include "src/storage/blobfs/common.h"

namespace blobfs {

// Formats the underlying device with an empty Blobfs partition.
zx_status_t FormatFilesystem(block_client::BlockDevice* device, const FilesystemOptions& options);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_MKFS_H_
