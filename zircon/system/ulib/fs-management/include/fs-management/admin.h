// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_MANAGEMENT_ADMIN_H_
#define FS_MANAGEMENT_ADMIN_H_

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

typedef struct init_options {
  bool readonly;
  bool verbose_mount;
  bool collect_metrics;
  // Ensures that requests to the mountpoint will be propagated to the underlying FS
  bool wait_until_ready;
  // Enable journaling on the file system (if supported).
  bool enable_journal;
  // Enable paging on the file system (if supported).
  bool enable_pager;
  // An optional compression algorithm specifier for the filesystem to use when storing files (if
  // the filesystem supports it).
  const char* write_compression_algorithm;
  // An optional compression level for the filesystem to use when storing files (if the filesystem
  // and the configured |write_compression_algorithm| supports it).
  // Setting to < 0 indicates no value (the filesystem chooses a default if necessary).
  int write_compression_level;
  // If true, run fsck after every transaction (if supported). This is for testing/debugging
  // purposes.
  bool fsck_after_every_transaction;
  // Provide a launch callback function pointer for configuring how the underlying filesystem
  // process is launched.
  LaunchCallback callback;
} init_options_t;

__EXPORT
extern const init_options_t default_init_options;

typedef struct mkfs_options {
  uint32_t fvm_data_slices;
  bool verbose;
  // The number of sectors per cluster on a FAT file systems or zero for the default.
  int sectors_per_cluster;
} mkfs_options_t;

__EXPORT
extern const mkfs_options_t default_mkfs_options;

typedef struct fsck_options {
  bool verbose;
  // At MOST one of the following '*_modify' flags may be true.
  bool never_modify;   // Fsck still looks for problems, but it does not try to resolve them.
  bool always_modify;  // Fsck never asks to resolve problems; it assumes it should fix them.
  bool force;          // Force fsck to check the filesystem integrity, even if it is "clean".
  bool apply_journal;  // Apply journal prior to running the consistency checker.
} fsck_options_t;

__EXPORT
extern const fsck_options_t default_fsck_options;

// Format the provided device with a requested disk format.
zx_status_t mkfs(const char* device_path, disk_format_t df, LaunchCallback cb,
                 const mkfs_options_t* options);

// Check and repair a device with a requested disk format.
zx_status_t fsck(const char* device_path, disk_format_t df, const fsck_options_t* options,
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
zx_status_t fs_init(zx_handle_t device_handle, disk_format_t df, const init_options_t* options,
                    zx_handle_t* out_export_root);

// Register the filesystem outgoing directory with the fshost registry service. This optional step
// allows filesystem services to be accessed by sufficiently priviledged processes.
//
// |export_root| is never consumed.
zx_status_t fs_register(zx_handle_t export_root);

// Get a connection to the root of the filesystem, given a filesystem outgoing directory.
//
// |export_root| is never consumed.
zx_status_t fs_root_handle(zx_handle_t export_root, zx_handle_t* out_root);

#endif  // FS_MANAGEMENT_ADMIN_H_
