// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/device/ioctl.h>

#define IOCTL_CONSOLE_GET_DIMENSIONS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CONSOLE, 1)
#define IOCTL_CONSOLE_SET_ACTIVE_VC \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CONSOLE, 2)

typedef struct {
    uint32_t width;
    uint32_t height;
} ioctl_console_dimensions_t;

