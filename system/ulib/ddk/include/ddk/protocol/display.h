// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/ioctl.h>
#include <magenta/types.h>
#include <magenta/pixelformat.h>

/**
 * protocol/display.h - display protocol definitions
 */

#define MX_DISPLAY_FLAG_HW_FRAMEBUFFER (1 << 0)

typedef struct mx_display_info {
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixelsize;
    uint32_t flags;
} mx_display_info_t;

typedef struct mx_display_protocol {
    mx_status_t (*set_mode)(mx_device_t* dev, mx_display_info_t* info);
    // sets the display mode

    mx_status_t (*get_mode)(mx_device_t* dev, mx_display_info_t* info);
    // gets the display mode

    mx_status_t (*get_framebuffer)(mx_device_t* dev, void** framebuffer);
    // gets a pointer to the framebuffer

    void (*flush)(mx_device_t* dev);
    // flushes the framebuffer
} mx_display_protocol_t;

#define IOCTL_DISPLAY_GET_FB \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DISPLAY, 1)
#define IOCTL_DISPLAY_FLUSH_FB \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DISPLAY, 2)

typedef struct {
    mx_handle_t vmo;
    mx_display_info_t info;
} ioctl_display_get_fb_t;
