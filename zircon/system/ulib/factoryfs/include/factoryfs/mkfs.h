// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACTORYFS_MKFS_H_
#define FACTORYFS_MKFS_H_

#include <block-client/cpp/block-device.h>

namespace factoryfs {

// Formats the underlying device with an empty Factoryfs partition.
zx_status_t FormatFilesystem(block_client::BlockDevice* device);

}  // namespace factoryfs

#endif  // FACTORYFS_MKFS_H_
