// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FACTORY_FACTORYFS_MOUNT_H_
#define SRC_STORAGE_FACTORY_FACTORYFS_MOUNT_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>

#include "src/lib/storage/block_client/cpp/block_device.h"

namespace factoryfs {

// Toggles that may be set on factoryfs during initialization.
struct MountOptions {
  bool verbose = false;
  bool metrics = false;  // TODO(manalib)
};

// Begins serving requests to the filesystem by parsing the on-disk format using |device|.
// This function blocks until the filesystem terminates.
zx_status_t Mount(std::unique_ptr<block_client::BlockDevice> device, MountOptions* options,
                  fidl::ServerEnd<fuchsia_io::Directory> root);

}  // namespace factoryfs

#endif  // SRC_STORAGE_FACTORY_FACTORYFS_MOUNT_H_
