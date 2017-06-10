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
} ramdisk_ioctl_config_t;

typedef struct ramdisk_ioctl_config_response {
    char name[NAME_MAX + 1];
} ramdisk_ioctl_config_response_t;

// ssize_t ioctl_ramdisk_config(int fd, const ramdisk_ioctl_config_t* in,
//                              ramdisk_ioctl_config_response_t* out);
IOCTL_WRAPPER_INOUT(ioctl_ramdisk_config, IOCTL_RAMDISK_CONFIG, ramdisk_ioctl_config_t,
                    ramdisk_ioctl_config_response_t);

// ssize_t ioctl_ramdisk_unlink(int fd);
IOCTL_WRAPPER(ioctl_ramdisk_unlink, IOCTL_RAMDISK_UNLINK);
