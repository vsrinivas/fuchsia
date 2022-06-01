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

enum class Writability {
  // Do not write to persistent storage under any circumstances whatsoever.
  ReadOnlyDisk,
  // Do not allow users of the filesystem to mutate filesystem state. This state allows the journal
  // to replay while initializing writeback.
  ReadOnlyFilesystem,
  // Permit all operations.
  Writable,
};

struct MountOptions {
  Writability writability = Writability::Writable;
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

// Start the filesystem on the block device backed by |bcache|, and serve it on |root|. Blocks
// until the filesystem terminates.
zx::status<> Mount(std::unique_ptr<minfs::Bcache> bcache, const MountOptions& options,
                   fidl::ServerEnd<fuchsia_io::Directory> root);

#endif  // __Fuchsia__

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_MOUNT_H_
