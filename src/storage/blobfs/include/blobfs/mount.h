// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_MOUNT_H_
#define SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_MOUNT_H_

#include <lib/async-loop/default.h>
#include <lib/zx/channel.h>
#include <lib/zx/resource.h>

#include <blobfs/cache-policy.h>
#include <blobfs/compression-settings.h>
#include <block-client/cpp/block-device.h>
#include <fbl/function.h>

namespace blobfs {

using block_client::BlockDevice;

// TODO(fxbug.dev/54521): This is a temporary measure. The diagnostics directory can
// eventually be added to the outgoing dir passed via PA_DIRECTORY_REQUEST.
#define FS_HANDLE_DIAGNOSTICS_DIR PA_HND(PA_USER0, 2)

// Determines the kind of directory layout the filesystem server should expose to the outside world.
// TODO(fxbug.dev/34531): When all users migrate to the export directory, delete this enum, since only
// |kExportDirectory| would be used.
enum class ServeLayout {
  // The root of the filesystem is exposed directly
  kDataRootOnly,

  // Expose a pseudo-directory with the filesystem root located at "/root".
  // TODO(fxbug.dev/34531): Also expose an administration service under "/svc/fuchsia.fs.Admin".
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
  bool verbose = false;
  bool metrics = false;
  bool journal = false;
  bool pager = false;
  CachePolicy cache_policy = CachePolicy::EvictImmediately;
  CompressionSettings compression_settings{};
};

// Begins serving requests to the filesystem by parsing the on-disk format using |device|. If
// |ServeLayout| is |kDataRootOnly|, |root| serves the root of the filesystem. If it's
// |kExportDirectory|, |root| serves an outgoing directory.
//
// blobfs relies on the zx_vmo_replace_as_executable syscall to be able to serve executable blobs.
// The caller must either pass a valid Resource handle of kind ZX_RSRC_KIND_VMEX (or _ROOT) for
// |vmex_resource|, or else the mounted filesystem will not support requesting VMOs for blobs with
// VMO_FLAG_EXEC.
//
// |diagnostics_dir| is the server end of a diagnostics directory made for BlobFS.
// The inspect tree is served in this directory. This directory will be visible to Archivist.
//
// This function blocks until the filesystem terminates.
zx_status_t Mount(std::unique_ptr<BlockDevice> device, MountOptions* options, zx::channel root,
                  ServeLayout layout, zx::resource vmex_resource, zx::channel diagnostics_dir);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_INCLUDE_BLOBFS_MOUNT_H_
