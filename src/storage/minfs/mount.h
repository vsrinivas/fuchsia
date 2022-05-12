// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_MOUNT_H_
#define SRC_STORAGE_MINFS_MOUNT_H_

#include <memory>

#ifdef __Fuchsia__
#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#include "src/storage/minfs/bcache.h"
#endif

namespace minfs {

struct MountOptions {
  // When true, no changes are made to the file-system, including marking the volume as clean. This
  // differs from readonly_after_initialization which might replay the journal and mark the volume
  // as clean.
  // TODO(fxbug.dev/51056): Unify the readonly and readonly_after_initialization flags.
  bool readonly = false;
  // Determines whether the filesystem will be accessible as read-only.
  // This does not mean that access to the block device is exclusively read-only;
  // the filesystem can still perform internal operations (like journal replay)
  // while "read-only".
  //
  // The "clean bit" is written to storage if "readonly = false".
  bool readonly_after_initialization = false;
  bool verbose = false;
  // Determines if the filesystem performs actions like replaying the journal, repairing the
  // superblock, etc.
  bool repair_filesystem = true;
  // For testing only: if true, run fsck after every transaction.
  bool fsck_after_every_transaction = false;

  // Number of slices to preallocate for data when the filesystem is created.
  uint32_t fvm_data_slices = 1;

  // If true, don't log messages except for errors.
  bool quiet = false;
};

#ifdef __Fuchsia__
struct CreateBcacheResult {
  std::unique_ptr<minfs::Bcache> bcache;
  bool is_read_only;
};

// Creates a Bcache using |device|.
//
// Returns the bcache and a boolean indicating if the underlying device is read-only.
zx::status<CreateBcacheResult> CreateBcache(std::unique_ptr<block_client::BlockDevice> device);

// Mount the filesystem backed by |bcache| and serve under the provided |mount_channel|.
// The layout of the served directory is controlled by |serve_layout|.
//
// This function does not start the async_dispatcher_t object owned by |vfs|;
// requests will not be dispatched if that async_dispatcher_t object is not
// active.
zx::status<std::unique_ptr<fs::ManagedVfs>> MountAndServe(const MountOptions& options,
                                                          async_dispatcher_t* dispatcher,
                                                          std::unique_ptr<minfs::Bcache> bcache,
                                                          zx::channel mount_channel,
                                                          fit::closure on_unmount);

#endif  // __Fuchsia__

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_MOUNT_H_
