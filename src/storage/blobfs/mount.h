// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_MOUNT_H_
#define SRC_STORAGE_BLOBFS_MOUNT_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/default.h>
#include <lib/zx/resource.h>

#include <optional>

#include <block-client/cpp/block-device.h>
#include <cobalt-client/cpp/collector.h>
#include <fbl/function.h>

#include "src/storage/blobfs/cache_policy.h"
#include "src/storage/blobfs/compression_settings.h"

namespace blobfs {

using block_client::BlockDevice;

// Determines the kind of directory layout the filesystem server should expose to the outside world.
// TODO(fxbug.dev/34531): When all users migrate to the export directory, delete this enum, since
// only |kExportDirectory| would be used.
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
  // Do not allow users of the filesystem to mutate filesystem state. This state allows the journal
  // to replay while initializing writeback.
  ReadOnlyFilesystem,
  // Permit all operations.
  Writable,
};

// Time between each Cobalt flush. Flushing data too frequently leads to collecting large amount of
// data in cobalt.
constexpr uint32_t kMetricsFlushTimeMinutes = 5;
constexpr zx::duration kMetricsFlushTime = zx::min(kMetricsFlushTimeMinutes);

// Toggles that may be set on blobfs during initialization.
struct MountOptions {
  Writability writability = Writability::Writable;
  bool verbose = false;
  bool metrics = false;
  CachePolicy cache_policy = CachePolicy::EvictImmediately;

  // Optional overriden cache policy for pager-backed blobs.
  std::optional<CachePolicy> pager_backed_cache_policy = std::nullopt;

  CompressionSettings compression_settings{};
  bool sandbox_decompression = false;

  int32_t paging_threads = 1;
#ifndef NDEBUG
  bool fsck_at_end_of_every_transaction = false;
#endif

  // Custom function to help install custom logger. Used during unit testing.
  std::function<std::unique_ptr<cobalt_client::Collector>()> collector_factory;

  // Time between two metrics flushes.
  zx::duration metrics_flush_time = kMetricsFlushTime;
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
zx_status_t Mount(std::unique_ptr<BlockDevice> device, const MountOptions& options,
                  fidl::ServerEnd<fuchsia_io::Directory> root, ServeLayout layout,
                  zx::resource vmex_resource);

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_MOUNT_H_
