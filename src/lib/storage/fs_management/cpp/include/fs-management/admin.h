// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_INCLUDE_FS_MANAGEMENT_ADMIN_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_INCLUDE_FS_MANAGEMENT_ADMIN_H_

#include <zircon/types.h>

#include <fs-management/format.h>
#include <fs-management/launch.h>

#define PATH_DATA "/data"
#define PATH_INSTALL "/install"
#define PATH_DURABLE "/durable"
#define PATH_SYSTEM "/system"
#define PATH_BLOB "/blob"
#define PATH_FACTORY "/factory"
#define PATH_VOLUME "/volume"
#define PATH_DEV_BLOCK "/dev/class/block"

struct InitOptions {
  bool readonly = false;
  bool verbose_mount = false;
  bool collect_metrics = false;

  // Ensures that requests to the mountpoint will be propagated to the underlying FS
  bool wait_until_ready = true;

  // An optional compression algorithm specifier for the filesystem to use when storing files (if
  // the filesystem supports it).
  const char* write_compression_algorithm = nullptr;

  // An optional compression level for the filesystem to use when storing files (if the filesystem
  // and the configured |write_compression_algorithm| supports it).
  // Setting to < 0 indicates no value (the filesystem chooses a default if necessary).
  int write_compression_level = -1;

  // An optional eviction policy specifier for the filesystem to use for in-memory structures (if
  // the filesystem supports it).
  const char* cache_eviction_policy = nullptr;

  // If true, run fsck after every transaction (if supported). This is for testing/debugging
  // purposes.
  bool fsck_after_every_transaction = false;

  // If true, decompression is run in a sandbox component.
  bool sandbox_decompression = false;

  // Provide a launch callback function pointer for configuring how the underlying filesystem
  // process is launched.
  LaunchCallback callback = &launch_stdio_async;
};

struct MkfsOptions {
  uint32_t fvm_data_slices = 1;
  bool verbose = false;

  // The number of sectors per cluster on a FAT file systems or zero for the default.
  int sectors_per_cluster = 0;

  // Set to use the deprecated padded blobfs format.
  bool deprecated_padded_blobfs_format = false;

  // The initial number of inodes to allocate space for. If 0, a default is used. Only supported
  // for blobfs.
  uint64_t num_inodes = 0;
};

struct FsckOptions {
  bool verbose = false;

  // At MOST one of the following '*_modify' flags may be true.
  bool never_modify = false;   // Fsck still looks for problems, but does not try to resolve them.
  bool always_modify = false;  // Fsck never asks to resolve problems; it will always do it.
  bool force = false;          // Force fsck to check the filesystem integrity, even if "clean".
};

// Format the provided device with a requested disk format.
zx_status_t mkfs(const char* device_path, disk_format_t df, LaunchCallback cb,
                 const MkfsOptions& options);

// Check and repair a device with a requested disk format.
zx_status_t fsck(const char* device_path, disk_format_t df, const FsckOptions& options,
                 LaunchCallback cb);

// Initialize the filesystem present on |device_handle|, returning a connection to the outgoing
// directory in |out_export_root|. The outgoing directory implements |fuchsia.io/Directory| and
// contains handles to services exported by the filesystem.
//
// The outgoing directory has the following layout -
//     |/root| - the data root of the filesystem
//
// Specific filesystems may have additional entries in the outgoing directory for
// filesystem-specific operations.
//
// |device_handle| is always consumed.
zx_status_t fs_init(zx_handle_t device_handle, disk_format_t df, const InitOptions& options,
                    zx_handle_t* out_export_root);

// Get a connection to the root of the filesystem, given a filesystem outgoing directory.
//
// |export_root| is never consumed.
zx_status_t fs_root_handle(zx_handle_t export_root, zx_handle_t* out_root);

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_INCLUDE_FS_MANAGEMENT_ADMIN_H_
