// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

#define IOCTL_DEVMGR_MOUNT_FS \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVMGR, 0)
// Unmount the filesystem which 'fd' belongs to.
#define IOCTL_DEVMGR_UNMOUNT_FS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVMGR, 1)
// If a filesystem is mounted on the node represented by 'fd', unmount it.
#define IOCTL_DEVMGR_UNMOUNT_NODE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVMGR, 2)

// ssize_t ioctl_devmgr_mount_fs(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_devmgr_mount_fs, IOCTL_DEVMGR_MOUNT_FS, mx_handle_t);

// ssize_t ioctl_devmgr_unmount_fs(int fd);
IOCTL_WRAPPER(ioctl_devmgr_unmount_fs, IOCTL_DEVMGR_UNMOUNT_FS);

// ssize_t ioctl_devmgr_unmount_node(int fd);
IOCTL_WRAPPER(ioctl_devmgr_unmount_node, IOCTL_DEVMGR_UNMOUNT_NODE);
