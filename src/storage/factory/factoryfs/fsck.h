// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functionality for checking the consistency of Factoryfs.

#ifndef SRC_STORAGE_FACTORY_FACTORYFS_FSCK_H_
#define SRC_STORAGE_FACTORY_FACTORYFS_FSCK_H_

#include <block-client/cpp/block-device.h>

#include "src/storage/factory/factoryfs/mount.h"

namespace factoryfs {

zx_status_t Fsck(std::unique_ptr<block_client::BlockDevice> device, MountOptions* options);

}  // namespace factoryfs

#endif  // SRC_STORAGE_FACTORY_FACTORYFS_FSCK_H_
