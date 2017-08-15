// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl-wrapper.h>
#include <magenta/device/ioctl.h>
#include <magenta/types.h>

#define MAX_FS_NAME_LEN 32

#define IOCTL_VFS_MOUNT_FS \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_VFS, 0)
// Unmount the filesystem which 'fd' belongs to. Requires O_ADMIN,
// which is only provided with the original iostate from the
// root Vnode of a mounted filesystem.
#define IOCTL_VFS_UNMOUNT_FS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_VFS, 1)
// If a filesystem is mounted on the node represented by 'fd', detach
// the connection to the filesystem and return it.
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
// Given a VMO and a file name, create a file from the VMO
// with the provided name.
//
// The VMO handle must be the ONLY open handle to the VMO;
// otherwise, it has the risk of being resized from
// underneath the filesystem. If there are multiple handles
// open to the vmo (or the handle is not a VMO) the request
// will fail. If the provided VMO is mapped into a VMAR, the
// underlying pages will still be accessible to whoever
// can access the VMAR.
//
// This ioctl is currently only supported by MemFS.
#define IOCTL_VFS_VMO_CREATE \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_VFS, 7)

// Watch a directory for changes
// in: vfs_watch_dir_t
//
// Watch event messages are sent via the provided channel and take the form:
// { uint8_t event; uint8_t namelen; uint8_t name[namelen]; }
// Multiple events may arrive in one message, one after another.
// Names do not include a terminating null.
#define IOCTL_VFS_WATCH_DIR \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_VFS, 8)

// Return path of block device underlying the filesystem. Requires O_ADMIN.
#define IOCTL_VFS_GET_DEVICE_PATH \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_VFS, 9)

typedef struct {
    mx_handle_t channel; // Channel to which watch events will be sent
    uint32_t mask;       // Bitmask of desired events (1 << WATCH_EVT_*)
    uint32_t options;    // Options.  Must be zero.
} vfs_watch_dir_t;

// Indicates that the directory being watched has been deleted.
// namelen will be 0
#define VFS_WATCH_EVT_DELETED 0

// Indication of a file that has been added (created or moved
// in) to the directory
#define VFS_WATCH_EVT_ADDED 1

// Indication of a file that has been removed (deleted or moved
// out) from the directory
#define VFS_WATCH_EVT_REMOVED 2

// Indication of file already in directory when watch started
#define VFS_WATCH_EVT_EXISTING 3

// Indication that no more EXISTING events will be sent (client
// has been informed of all pre-existing files in this directory)
// namelen will be 0
#define VFS_WATCH_EVT_IDLE 4

typedef struct {
    uint8_t event;
    uint8_t len;
    char name[];
} vfs_watch_msg_t;

// clang-format off

#define VFS_WATCH_EVT_MASK(e)   (1u << (e))
#define VFS_WATCH_MASK_DELETED  VFS_WATCH_EVT_MASK(VFS_WATCH_EVT_DELETED)
#define VFS_WATCH_MASK_ADDED    VFS_WATCH_EVT_MASK(VFS_WATCH_EVT_ADDED)
#define VFS_WATCH_MASK_REMOVED  VFS_WATCH_EVT_MASK(VFS_WATCH_EVT_REMOVED)
#define VFS_WATCH_MASK_EXISTING VFS_WATCH_EVT_MASK(VFS_WATCH_EVT_EXISTING)
#define VFS_WATCH_MASK_IDLE     VFS_WATCH_EVT_MASK(VFS_WATCH_EVT_IDLE)
#define VFS_WATCH_MASK_ALL      (0x1Fu)

// clang-format on

#define VFS_WATCH_NAME_MAX 255
#define VFS_WATCH_MSG_MAX 8192

// ssize_t ioctl_vfs_mount_fs(int fd, mx_handle_t* in);
IOCTL_WRAPPER_IN(ioctl_vfs_mount_fs, IOCTL_VFS_MOUNT_FS, mx_handle_t);

// ssize_t ioctl_vfs_unmount_fs(int fd);
IOCTL_WRAPPER(ioctl_vfs_unmount_fs, IOCTL_VFS_UNMOUNT_FS);

// ssize_t ioctl_vfs_unmount_node(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_vfs_unmount_node, IOCTL_VFS_UNMOUNT_NODE, mx_handle_t);

// ssize_t ioctl_vfs_mount_bootfs_vmo(int fd, mx_handle_t* in);
IOCTL_WRAPPER_IN(ioctl_vfs_mount_bootfs_vmo, IOCTL_VFS_MOUNT_BOOTFS_VMO, mx_handle_t);

typedef struct vfs_query_info {
    // these are the total/used # of data bytes, not # of entire disk bytes
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t total_nodes;
    uint64_t used_nodes;
    char name[]; // Does not include null-terminator
} vfs_query_info_t;

// ssize_t ioctl_vfs_query_fs(int fd, vfs_query_info_t* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_vfs_query_fs, IOCTL_VFS_QUERY_FS, vfs_query_info_t);

// ssize_t ioctl_vfs_get_token(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_vfs_get_token, IOCTL_VFS_GET_TOKEN, mx_handle_t);

// ssize_t ioctl_vfs_watch_dir(int fd, vfs_watch_dir_t* in);
IOCTL_WRAPPER_IN(ioctl_vfs_watch_dir, IOCTL_VFS_WATCH_DIR, vfs_watch_dir_t);

// ssize_t ioctl_vfs_get_device_path(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_vfs_get_device_path, IOCTL_VFS_GET_DEVICE_PATH, char);

typedef struct {
    mx_handle_t vmo;
    char name[]; // Null-terminator required
} vmo_create_config_t;

// ssize_t ioctl_vfs_vmo_create(int fd, vmo_create_config_t* in, size_t in_len);
IOCTL_WRAPPER_VARIN(ioctl_vfs_vmo_create, IOCTL_VFS_VMO_CREATE, vmo_create_config_t);

#define MOUNT_MKDIR_FLAG_REPLACE 1

typedef struct mount_mkdir_config {
    mx_handle_t fs_root;
    uint32_t flags;
    char name[]; // Null-terminator required
} mount_mkdir_config_t;

// ssize_t ioctl_vfs_mount_mkdir_fs(int fd, mount_mkdir_config_t* in, size_t in_len);
IOCTL_WRAPPER_VARIN(ioctl_vfs_mount_mkdir_fs, IOCTL_VFS_MOUNT_MKDIR_FS, mount_mkdir_config_t);
