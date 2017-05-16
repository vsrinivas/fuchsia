// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

#define IOCTL_VFS_MOUNT_FS \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_VFS, 0)
// Unmount the filesystem which 'fd' belongs to.
#define IOCTL_VFS_UNMOUNT_FS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_VFS, 1)
// If a filesystem is mounted on the node represented by 'fd', unmount it.
#define IOCTL_VFS_UNMOUNT_NODE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_VFS, 2)
// Add a bootfs vmo to the system fs.
#define IOCTL_VFS_MOUNT_BOOTFS_VMO \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_VFS, 3)
// Determine which filesystem the vnode belongs to.
#define IOCTL_VFS_QUERY_FS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_VFS, 4)
#define IOCTL_VFS_GET_TOKEN \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_VFS, 5)
#define IOCTL_VFS_MOUNT_MKDIR_FS \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_VFS, 6)
// Watch a directory for changes
//   in: none
//   out: handle to channel to get notified on.
//        Notification messages will be a string child entry name WITHOUT a
//        null-terminating character; the length of the string should be
//        determined by the message length during the channel read.
#define IOCTL_VFS_WATCH_DIR \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_VFS, 7)

// ssize_t ioctl_vfs_mount_fs(int fd, mx_handle_t* in);
IOCTL_WRAPPER_IN(ioctl_vfs_mount_fs, IOCTL_VFS_MOUNT_FS, mx_handle_t);

// ssize_t ioctl_vfs_unmount_fs(int fd);
IOCTL_WRAPPER(ioctl_vfs_unmount_fs, IOCTL_VFS_UNMOUNT_FS);

// ssize_t ioctl_vfs_unmount_node(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_vfs_unmount_node, IOCTL_VFS_UNMOUNT_NODE, mx_handle_t);

// ssize_t ioctl_vfs_mount_bootfs_vmo(int fd, mx_handle_t* in);
IOCTL_WRAPPER_IN(ioctl_vfs_mount_bootfs_vmo, IOCTL_VFS_MOUNT_BOOTFS_VMO, mx_handle_t);

// ssize_t ioctl_vfs_query_fs(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_vfs_query_fs, IOCTL_VFS_QUERY_FS, char);

// ssize_t ioctl_vfs_get_token(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_vfs_get_token, IOCTL_VFS_GET_TOKEN, mx_handle_t);

// ssize_t ioctl_vfs_watch_dir(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_vfs_watch_dir, IOCTL_VFS_WATCH_DIR, mx_handle_t);

#define MOUNT_MKDIR_FLAG_REPLACE 1

typedef struct mount_mkdir_config {
    mx_handle_t fs_root;
    uint32_t flags;
    char name[]; // Null-terminator required
} mount_mkdir_config_t;

// ssize_t ioctl_vfs_mount_mkdir_fs(int fd, mount_mkdir_config_t* in, size_t in_len);
IOCTL_WRAPPER_VARIN(ioctl_vfs_mount_mkdir_fs, IOCTL_VFS_MOUNT_MKDIR_FS, mount_mkdir_config_t);
