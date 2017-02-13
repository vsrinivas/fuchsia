// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

#define IOCTL_RAMDISK_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 1)
#define IOCTL_RAMDISK_UNLINK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RAMDISK, 2)

typedef struct ramdisk_ioctl_config {
    uint64_t blk_size;
    uint64_t blk_count;
    char name[NAME_MAX];
} ramdisk_ioctl_config_t;

// ssize_t ioctl_ramdisk_config(int fd, const ramdisk_ioctl_config_t* in);
IOCTL_WRAPPER_IN(ioctl_ramdisk_config, IOCTL_RAMDISK_CONFIG, ramdisk_ioctl_config_t);

// ssize_t ioctl_ramdisk_unlink(int fd);
IOCTL_WRAPPER(ioctl_ramdisk_unlink, IOCTL_RAMDISK_UNLINK);
