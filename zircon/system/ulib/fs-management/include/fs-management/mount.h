// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_MANAGEMENT_MOUNT_H_
#define FS_MANAGEMENT_MOUNT_H_

#include <zircon/compiler.h>

#include <fs-management/admin.h>
#include <fs-management/launch.h>

__BEGIN_CDECLS

typedef struct mount_options {
  bool readonly;
  bool verbose_mount;
  bool collect_metrics;
  // Ensures that requests to the mountpoint will be propagated to the underlying FS
  bool wait_until_ready;
  // Create the mountpoint directory if it doesn't already exist.
  // Must be false if passed to "fmount".
  bool create_mountpoint;
  // Enable journaling on the file system (if supported).
  bool enable_journal;
} mount_options_t;

__EXPORT
extern const mount_options_t default_mount_options;

// Given the following:
//  - A device containing a filesystem image of a known format
//  - A path on which to mount the filesystem
//  - Some configuration options for launching the filesystem, and
//  - A callback which can be used to 'launch' an fs server,
//
// Prepare the argv arguments to the filesystem process, mount a handle on the
// expected mount_path, and call the 'launch' callback (if the filesystem is
// recognized).
//
// device_fd is always consumed. If the callback is reached, then the 'device_fd'
// is transferred via handles to the callback arguments.
zx_status_t mount(int device_fd, const char* mount_path, disk_format_t df,
                  const mount_options_t* options, LaunchCallback cb);
// 'mount_fd' is used in lieu of the mount_path. It is not consumed (i.e.,
// it will still be open after this function completes, regardless of
// success or failure).
zx_status_t fmount(int device_fd, int mount_fd, disk_format_t df, const mount_options_t* options,
                   LaunchCallback cb);

// Umount the filesystem process.
//
// Returns ZX_ERR_BAD_STATE if mount_path could not be opened.
// Returns ZX_ERR_NOT_FOUND if there is no mounted filesystem on mount_path.
// Other errors may also be returned if problems occur while unmounting.
zx_status_t umount(const char* mount_path);
// 'mount_fd' is used in lieu of the mount_path. It is not consumed.
zx_status_t fumount(int mount_fd);

__END_CDECLS

#endif  // FS_MANAGEMENT_MOUNT_H_
