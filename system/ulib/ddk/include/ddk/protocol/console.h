// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/ioctl.h>
#include <magenta/types.h>

/**
 * protocol/console.h - console protocol definitions
 */

#define MX_CONSOLE_FLAG_BLOCKING 1
// readkey does not return until a key is available

typedef struct mx_protocol_console {
    mx_handle_t (*getsurface)(mx_device_t* dev, uint32_t* width, uint32_t* height);
    // returns a vmo pointing to an array of uint16_t's.

    void (*invalidate)(mx_device_t* dev, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    // invalidates an area in the surface

    void (*movecursor)(mx_device_t* dev, uint32_t x, uint32_t y, bool visible);
    // moves/hides/shows the cursor

    void (*setpalette)(mx_device_t* dev, uint32_t colors[16]);
    // install a new map of 16 XXRRGGBB values

    mx_status_t (*readkey)(mx_device_t* dev, uint32_t flags);
} mx_protocol_console_t;

#define IOCTL_CONSOLE_GET_DIMENSIONS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CONSOLE, 1)

typedef struct {
    uint32_t width;
    uint32_t height;
} ioctl_console_dimensions_t;