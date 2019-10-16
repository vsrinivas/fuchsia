// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_MKFS_H_
#define BLOBFS_MKFS_H_

#include <block-client/cpp/block-device.h>

namespace blobfs {

using block_client::BlockDevice;

// Formats the underlying device with an empty Blobfs partition.
zx_status_t FormatFilesystem(BlockDevice* device);

}  // namespace blobfs

#endif  // BLOBFS_MKFS_H_
