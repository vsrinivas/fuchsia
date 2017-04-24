// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

#define IOCTL_DEVMGR_MOUNT_FS \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_DEVMGR, 0)
// Unmount the filesystem which 'fd' belongs to.
#define IOCTL_DEVMGR_UNMOUNT_FS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVMGR, 1)
// If a filesystem is mounted on the node represented by 'fd', unmount it.
#define IOCTL_DEVMGR_UNMOUNT_NODE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVMGR, 2)
// Add a bootfs vmo to the system fs.
#define IOCTL_DEVMGR_MOUNT_BOOTFS_VMO \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_DEVMGR, 3)
// Determine which filesystem the vnode belongs to.
#define IOCTL_DEVMGR_QUERY_FS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVMGR, 4)
#define IOCTL_DEVMGR_GET_TOKEN \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVMGR, 5)
#define IOCTL_DEVMGR_MOUNT_MKDIR_FS \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_DEVMGR, 6)

// ssize_t ioctl_devmgr_mount_fs(int fd, mx_handle_t* in);
IOCTL_WRAPPER_IN(ioctl_devmgr_mount_fs, IOCTL_DEVMGR_MOUNT_FS, mx_handle_t);

// ssize_t ioctl_devmgr_unmount_fs(int fd);
IOCTL_WRAPPER(ioctl_devmgr_unmount_fs, IOCTL_DEVMGR_UNMOUNT_FS);

// ssize_t ioctl_devmgr_unmount_node(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_devmgr_unmount_node, IOCTL_DEVMGR_UNMOUNT_NODE, mx_handle_t);

// ssize_t ioctl_devmgr_mount_bootfs_vmo(int fd, mx_handle_t* in);
IOCTL_WRAPPER_IN(ioctl_devmgr_mount_bootfs_vmo, IOCTL_DEVMGR_MOUNT_BOOTFS_VMO, mx_handle_t);

// ssize_t ioctl_devmgr_query_fs(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_devmgr_query_fs, IOCTL_DEVMGR_QUERY_FS, char);

// ssize_t ioctl_devmgr_get_token(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_devmgr_get_token, IOCTL_DEVMGR_GET_TOKEN, mx_handle_t);

#define MOUNT_MKDIR_FLAG_REPLACE 1

typedef struct mount_mkdir_config {
    mx_handle_t fs_root;
    uint32_t flags;
    char name[]; // Null-terminator required
} mount_mkdir_config_t;

// ssize_t ioctl_devmgr_mount_mkdir_fs(int fd, mount_mkdir_config_t* in, size_t in_len);
IOCTL_WRAPPER_VARIN(ioctl_devmgr_mount_mkdir_fs, IOCTL_DEVMGR_MOUNT_MKDIR_FS, mount_mkdir_config_t);

// TODO(smklein): Move these ioctls to a new location
#define IOCTL_BLOBSTORE_BLOB_INIT \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVMGR, 7)

typedef struct blob_ioctl_config {
    size_t size_data;
} blob_ioctl_config_t;

// ssize_t ioctl_blobstore_blob_init(int fd, const blob_ioctl_config_t* in);
IOCTL_WRAPPER_IN(ioctl_blobstore_blob_init, IOCTL_BLOBSTORE_BLOB_INIT, blob_ioctl_config_t);
