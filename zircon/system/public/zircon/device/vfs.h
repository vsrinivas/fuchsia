// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_VFS_H_
#define SYSROOT_ZIRCON_DEVICE_VFS_H_

#include <zircon/types.h>

// Rights
// The file may be read.
#define ZX_FS_RIGHT_READABLE 0x00000001
// The file may be written.
#define ZX_FS_RIGHT_WRITABLE 0x00000002
// The connection can mount and unmount filesystems.
#define ZX_FS_RIGHT_ADMIN 0x00000004
#define ZX_FS_RIGHTS 0x0000FFFF

// Flags
// If the file does not exist, it will be created.
#define ZX_FS_FLAG_CREATE 0x00010000
// The file must not exist, otherwise an error will be returned.
// Ignored without ZX_FS_FLAG_CREATE.
#define ZX_FS_FLAG_EXCLUSIVE 0x00020000
// Truncates the file before using it.
#define ZX_FS_FLAG_TRUNCATE 0x00040000
// Returns an error if the opened file is not a directory.
#define ZX_FS_FLAG_DIRECTORY 0x00080000
// The file is opened in append mode, seeking to the end of the file before each
// write.
#define ZX_FS_FLAG_APPEND 0x00100000
// If the endpoint of this request refers to a mount point, open the local
// directory, not the remote mount.
#define ZX_FS_FLAG_NOREMOTE 0x00200000
// The file underlying file should not be opened, just a reference to the file.
#define ZX_FS_FLAG_VNODE_REF_ONLY 0x00400000
// When the file has been opened, the server should transmit a description event.
// This event will be transmitted either on success or failure.
#define ZX_FS_FLAG_DESCRIBE 0x00800000

// Watch event messages are sent via the provided channel and take the form:
// { uint8_t event; uint8_t namelen; uint8_t name[namelen]; }
// Multiple events may arrive in one message, one after another.
// Names do not include a terminating null.
typedef struct {
    uint8_t event;
    uint8_t len;
    char name[];
} vfs_watch_msg_t;

#define VFS_TYPE_BLOBFS 0x9e694d21
#define VFS_TYPE_MINFS 0x6e694d21
#define VFS_TYPE_MEMFS 0x3e694d21
#endif  // SYSROOT_ZIRCON_DEVICE_VFS_H_
