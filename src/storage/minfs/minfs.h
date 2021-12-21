// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_MINFS_H_
#define SRC_STORAGE_MINFS_MINFS_H_

#include <inttypes.h>
#include <lib/zx/status.h>

#include <functional>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/function.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/ref_ptr.h>

#ifdef __Fuchsia__
#include <lib/async/dispatcher.h>

#include "src/lib/cobalt/cpp/cobalt_logger.h"
#include "src/lib/storage/vfs/cpp/managed_vfs.h"
#endif

#include <utility>

#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/format.h"

namespace minfs {

// Controls the validation-checking performed by minfs when loading
// structures from disk.
enum class IntegrityCheck {
  // Do not attempt to validate structures on load. This is useful
  // for inspection tools, which do not depend on the correctness
  // of on-disk structures.
  kNone,
  // Validate structures (locally) before usage. This is the
  // recommended option for mounted filesystems.
  kAll,
};

// Indicates whether to update backup superblock.
enum class UpdateBackupSuperblock {
  // Do not write the backup superblock.
  kNoUpdate,
  // Update the backup superblock.
  kUpdate,
};

// Determines the kind of directory layout the filesystem server should expose to the outside world.
// TODO(fxbug.dev/34531): When all users migrate to the export directory, delete this enum, since
// only |kExportDirectory| would be used.
enum class ServeLayout {
  // The root of the filesystem is exposed directly.
  kDataRootOnly,

  // Expose a pseudo-directory with the filesystem root located at "svc/root".
  // TODO(fxbug.dev/34531): Also expose an administration service under "svc/fuchsia.fs.Admin".
  kExportDirectory
};

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
  bool metrics = false;
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

#ifdef __Fuchsia__
  // A function that generates a Cobalt logger.  If not provided, a default logger is used so long
  // as the MountAndServe function is used.
  std::function<std::unique_ptr<cobalt::CobaltLogger>()> cobalt_factory;
#endif
};

uint32_t CalculateVsliceCount(const Superblock& superblock);

// Format the partition backed by |bc| as MinFS.
zx::status<> Mkfs(const MountOptions& options, Bcache* bc);

// Format the partition backed by |bc| as MinFS.
inline zx::status<> Mkfs(Bcache* bc) { return Mkfs({}, bc); }

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
                                                          fbl::Closure on_unmount,
                                                          ServeLayout serve_layout);
#endif

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_MINFS_H_
