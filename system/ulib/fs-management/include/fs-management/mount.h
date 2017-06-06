// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS

#define PATH_DATA "/data"
#define PATH_SYSTEM "/system"
#define PATH_BLOBSTORE "/blobstore"
#define PATH_VOLUME "/volume"
#define PATH_DEV_BLOCK "/dev/class/block"

typedef enum disk_format_type {
    DISK_FORMAT_UNKNOWN,
    DISK_FORMAT_GPT,
    DISK_FORMAT_MBR,
    DISK_FORMAT_MINFS,
    DISK_FORMAT_FAT,
    DISK_FORMAT_BLOBFS,
    DISK_FORMAT_FVM,
} disk_format_t;

disk_format_t detect_disk_format(int fd);

typedef struct mount_options {
    bool readonly;
    bool verbose_mount;
    // Ensures that requests to the mountpoint will be propagated to the underlying FS
    bool wait_until_ready;
    // Create the mountpoint directory if it doesn't already exist.
    // Must be false if passed to "fmount".
    bool create_mountpoint;
} mount_options_t;

static const mount_options_t default_mount_options = {
    .readonly = false,
    .verbose_mount = false,
    .wait_until_ready = true,
    .create_mountpoint = false,
};

typedef struct mkfs_options {
    bool verbose;
} mkfs_options_t;

static const mkfs_options_t default_mkfs_options = {
    .verbose = false,
};

#define NUM_MKFS_OPTIONS 1

typedef struct fsck_options {
    bool verbose;
    // At MOST one of the following '*_modify' flags may be true.
    bool never_modify;  // Fsck still looks for problems, but it does not try to resolve them.
    bool always_modify; // Fsck never asks to resolve problems; it assumes it should fix them.
    bool force;         // Force fsck to check the filesystem integrity, even if it is marked as "clean".
} fsck_options_t;

#define NUM_FSCK_OPTIONS 3

static const fsck_options_t default_fsck_options = {
    .verbose = false,
    .never_modify = false,
    .always_modify = false,
    .force = false,
};

typedef mx_status_t (*LaunchCallback)(int argc, const char** argv,
                                      mx_handle_t* hnd, uint32_t* ids, size_t len);

// Creates kernel logs, does not wait for process to terminate
mx_status_t launch_logs_async(int argc, const char** argv, mx_handle_t* handles,
                              uint32_t* types, size_t len);
// Creates stdio logs, waits for process to terminate
mx_status_t launch_stdio_sync(int argc, const char** argv, mx_handle_t* handles,
                              uint32_t* types, size_t len);
// Creates stdio logs, does not wait for process to terminate
mx_status_t launch_stdio_async(int argc, const char** argv, mx_handle_t* handles,
                               uint32_t* types, size_t len);

// Given the following:
//  - A device containing a filesystem image of a known format
//  - A path on which to mount the filesystem
//  - Some configuration options for launching the filesystem, and
//  - A callback which can be used to 'launch' an fs server,
//
// Prepare the argv arguments to the filesystem process, mount a handle on the
// expected mountpath, and call the 'launch' callback (if the filesystem is
// recognized).
//
// devicefd is always consumed. If the callback is reached, then the 'devicefd'
// is transferred via handles to the callback arguments.
mx_status_t mount(int devicefd, const char* mountpath, disk_format_t df,
                  const mount_options_t* options, LaunchCallback cb);
// 'mountfd' is used in lieu of the mountpath. It is not consumed (i.e.,
// it will still be open after this function completes, regardless of
// success or failure).
mx_status_t fmount(int devicefd, int mountfd, disk_format_t df,
                   const mount_options_t* options, LaunchCallback cb);

// Format the provided device with a requested disk format.
mx_status_t mkfs(const char* devicepath, disk_format_t df, LaunchCallback cb,
                 const mkfs_options_t* options);

// Check and repair a device with a requested disk format.
mx_status_t fsck(const char* devicepath, disk_format_t df,
                 const fsck_options_t* options, LaunchCallback cb);

// Umount the filesystem process.
//
// Returns MX_ERR_BAD_STATE if mountpath could not be opened.
// Returns MX_ERR_NOT_FOUND if there is no mounted filesystem on mountpath.
// Other errors may also be returned if problems occur while unmounting.
mx_status_t umount(const char* mountpath);
// 'mountfd' is used in lieu of the mountpath. It is not consumed.
mx_status_t fumount(int mountfd);

__END_CDECLS
