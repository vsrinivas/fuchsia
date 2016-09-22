// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

#define IOCTL_DEVMGR_MOUNT_FS \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVMGR, 0)

// ssize_t ioctl_devmgr_mount_fs(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_devmgr_mount_fs, IOCTL_DEVMGR_MOUNT_FS, mx_handle_t);
