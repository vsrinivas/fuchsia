// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_MOUNT_H_
#define BLOBFS_MOUNT_H_

#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>

#include <blobfs/cache-policy.h>
#include <block-client/cpp/block-device.h>
#include <fbl/function.h>

namespace blobfs {

using block_client::BlockDevice;

// Determines the kind of directory layout the filesystem server should expose to the outside world.
// TODO(fxb/34531): When all users migrate to the export directory, delete this enum, since only
// |kExportDirectory| would be used.
enum class ServeLayout {
  // The root of the filesystem is exposed directly
  kDataRootOnly,

  // Expose a pseudo-directory with the filesystem root located at "/root".
  // TODO(fxb/34531): Also expose an administration service under "/svc/fuchsia.fs.Admin".
  kExportDirectory
};

enum class Writability {
  // Do not write to persistent storage under any circumstances whatsoever.
  ReadOnlyDisk,
  // Do not allow users of the filesystem to mutate filesystem state. This
  // state allows the journal to replay while initializing writeback.
  ReadOnlyFilesystem,
  // Permit all operations.
  Writable,
};

// Toggles that may be set on blobfs during initialization.
struct MountOptions {
  Writability writability = Writability::Writable;
  bool metrics = false;
  bool journal = false;
  bool pager = false;
  CachePolicy cache_policy = CachePolicy::EvictImmediately;
};

// Begins serving requests to the filesystem by parsing the on-disk format using |device|. If
// |ServeLayout| is |kDataRootOnly|, |root| serves the root of the filesystem. If it's
// |kExportDirectory|, |root| serves an outgoing directory.
//
// This function blocks until the filesystem terminates.
zx_status_t Mount(std::unique_ptr<BlockDevice> device, MountOptions* options, zx::channel root,
                  ServeLayout layout);

}  // namespace blobfs

#endif  // BLOBFS_MOUNT_H_
