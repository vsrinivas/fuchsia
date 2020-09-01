// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_MANAGEMENT_MOUNT_H_
#define FS_MANAGEMENT_MOUNT_H_

#include <zircon/compiler.h>

#include <fs-management/admin.h>
#include <fs-management/launch.h>

typedef struct mount_options {
  bool readonly;
  bool verbose_mount;
  bool collect_metrics;
  // Ensures that requests to the mountpoint will be propagated to the underlying FS
  bool wait_until_ready;
  // Create the mountpoint directory if it doesn't already exist.
  // Must be false if passed to "fmount".
  bool create_mountpoint;
  // Enable journaling on the filesystem (if supported).
  bool enable_journal;
  // Enable paging on the filesystem (if supported).
  bool enable_pager;
  // An optional compression algorithm specifier for the filesystem to use when storing files (if
  // the filesystem supports it).
  const char* write_compression_algorithm;
  // An optional cache eviction policy specifier for the filesystem to use for in-memory data (if
  // the filesystem supports it).
  const char* cache_eviction_policy;
  // If true will register with /svc/fuchsia.fshost.Registry.
  bool register_fs;
  // If set, run fsck after every transaction.
  bool fsck_after_every_transaction;
  // If set, attach the filesystem with O_ADMIN, which will allow the use of the DirectoryAdmin
  // protocol.
  bool admin;
  // If set, provides the handle pair for the filesystem processes's outgoing directory. If
  // unspecified, it is assumed the caller doesn't need it. The server handle is *always* consumed,
  // even on error; the client handle is unowned.
  struct {
    zx_handle_t client, server;
  } outgoing_directory;
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

// Mounts the filesystem being served via root_handle (which is consumed) at mount_path.
zx_status_t mount_root_handle(zx_handle_t root_handle, const char* mount_path);

// Umount the filesystem process.
//
// Returns ZX_ERR_BAD_STATE if mount_path could not be opened.
// Returns ZX_ERR_NOT_FOUND if there is no mounted filesystem on mount_path.
// Other errors may also be returned if problems occur while unmounting.
zx_status_t umount(const char* mount_path);
// 'mount_fd' is used in lieu of the mount_path. It is not consumed.
zx_status_t fumount(int mount_fd);

#endif  // FS_MANAGEMENT_MOUNT_H_
