// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define PATH_DATA "/data"
#define PATH_INSTALL "/install"
#define PATH_SYSTEM "/system"
#define PATH_BLOB "/blob"
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
    DISK_FORMAT_ZXCRYPT,
    DISK_FORMAT_COUNT_,
} disk_format_t;

static const char* disk_format_string_[DISK_FORMAT_COUNT_] = {
        [DISK_FORMAT_UNKNOWN] = "unknown",
        [DISK_FORMAT_GPT] = "gpt",
        [DISK_FORMAT_MBR] = "mbr",
        [DISK_FORMAT_MINFS] = "minfs",
        [DISK_FORMAT_FAT] = "fat",
        [DISK_FORMAT_BLOBFS] = "blobfs",
        [DISK_FORMAT_FVM] = "fvm",
        [DISK_FORMAT_ZXCRYPT] = "zxcrypt"};

static inline const char* disk_format_string(disk_format_t fs_type) {
    return disk_format_string_[fs_type];
}

#define HEADER_SIZE 4096

static const uint8_t minfs_magic[16] = {
    0x21, 0x4d, 0x69, 0x6e, 0x46, 0x53, 0x21, 0x00,
    0x04, 0xd3, 0xd3, 0xd3, 0xd3, 0x00, 0x50, 0x38,
};

static const uint8_t blobfs_magic[16] = {
    0x21, 0x4d, 0x69, 0x9e, 0x47, 0x53, 0x21, 0xac,
    0x14, 0xd3, 0xd3, 0xd4, 0xd4, 0x00, 0x50, 0x98,
};

static const uint8_t gpt_magic[16] = {
    0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54,
    0x00, 0x00, 0x01, 0x00, 0x5c, 0x00, 0x00, 0x00,
};

static const uint8_t fvm_magic[8] = {
    0x46, 0x56, 0x4d, 0x20, 0x50, 0x41, 0x52, 0x54,
};

static const uint8_t zxcrypt_magic[16] = {
    0x5f, 0xe8, 0xf8, 0x00, 0xb3, 0x6d, 0x11, 0xe7,
    0x80, 0x7a, 0x78, 0x63, 0x72, 0x79, 0x70, 0x74,
};

disk_format_t detect_disk_format(int fd);

typedef struct mount_options {
    bool readonly;
    bool verbose_mount;
    bool collect_metrics;
    // Ensures that requests to the mountpoint will be propagated to the underlying FS
    bool wait_until_ready;
    // Create the mountpoint directory if it doesn't already exist.
    // Must be false if passed to "fmount".
    bool create_mountpoint;
} mount_options_t;

extern const mount_options_t default_mount_options;

typedef struct mkfs_options {
    bool verbose;
} mkfs_options_t;

extern const mkfs_options_t default_mkfs_options;

#define NUM_MKFS_OPTIONS 1

typedef struct fsck_options {
    bool verbose;
    // At MOST one of the following '*_modify' flags may be true.
    bool never_modify;  // Fsck still looks for problems, but it does not try to resolve them.
    bool always_modify; // Fsck never asks to resolve problems; it assumes it should fix them.
    bool force;         // Force fsck to check the filesystem integrity, even if it is marked as "clean".
} fsck_options_t;

#define NUM_FSCK_OPTIONS 3

extern const fsck_options_t default_fsck_options;

typedef zx_status_t (*LaunchCallback)(int argc, const char** argv,
                                      zx_handle_t* hnd, uint32_t* ids, size_t len);

// Creates no logs, waits for process to terminate.
zx_status_t launch_silent_sync(int argc, const char** argv, zx_handle_t* handles,
                               uint32_t* types, size_t len);
// Creates no logs, does not wait for process to terminate.
zx_status_t launch_silent_async(int argc, const char** argv, zx_handle_t* handles,
                                uint32_t* types, size_t len);
// Creates stdio logs, waits for process to terminate.
zx_status_t launch_stdio_sync(int argc, const char** argv, zx_handle_t* handles,
                              uint32_t* types, size_t len);
// Creates stdio logs, does not wait for process to terminate.
zx_status_t launch_stdio_async(int argc, const char** argv, zx_handle_t* handles,
                               uint32_t* types, size_t len);
// Creates kernel logs, does not wait for process to terminate.
zx_status_t launch_logs_async(int argc, const char** argv, zx_handle_t* handles,
                              uint32_t* types, size_t len);

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
zx_status_t fmount(int device_fd, int mount_fd, disk_format_t df,
                   const mount_options_t* options, LaunchCallback cb);

// Format the provided device with a requested disk format.
zx_status_t mkfs(const char* device_path, disk_format_t df, LaunchCallback cb,
                 const mkfs_options_t* options);

// Check and repair a device with a requested disk format.
zx_status_t fsck(const char* device_path, disk_format_t df,
                 const fsck_options_t* options, LaunchCallback cb);

// Umount the filesystem process.
//
// Returns ZX_ERR_BAD_STATE if mount_path could not be opened.
// Returns ZX_ERR_NOT_FOUND if there is no mounted filesystem on mount_path.
// Other errors may also be returned if problems occur while unmounting.
zx_status_t umount(const char* mount_path);
// 'mount_fd' is used in lieu of the mount_path. It is not consumed.
zx_status_t fumount(int mount_fd);

__END_CDECLS
