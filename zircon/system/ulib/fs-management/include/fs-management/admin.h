// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_MANAGEMENT_ADMIN_H_
#define FS_MANAGEMENT_ADMIN_H_

#include <zircon/types.h>

#include <fs-management/format.h>
#include <fs-management/launch.h>

__BEGIN_CDECLS

#define PATH_DATA "/data"
#define PATH_INSTALL "/install"
#define PATH_SYSTEM "/system"
#define PATH_BLOB "/blob"
#define PATH_VOLUME "/volume"
#define PATH_DEV_BLOCK "/dev/class/block"

typedef struct mkfs_options {
  uint32_t fvm_data_slices;
  bool verbose;
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

__END_CDECLS

#endif  // FS_MANAGEMENT_ADMIN_H_
