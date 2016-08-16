// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/types.h>

/**
 * protocol/display.h - display protocol definitions
 */

#define MX_DISPLAY_FORMAT_NONE (-1)
#define MX_DISPLAY_FORMAT_RGB_565 (0)
#define MX_DISPLAY_FORMAT_RGB_332 (1)
#define MX_DISPLAY_FORMAT_RGB_2220 (2)
#define MX_DISPLAY_FORMAT_ARGB_8888 (3)
#define MX_DISPLAY_FORMAT_RGB_x888 (4)
#define MX_DISPLAY_FORMAT_MONO_1 (5)
#define MX_DISPLAY_FORMAT_MONO_8 (6)

#define MX_DISPLAY_FLAG_HW_FRAMEBUFFER (1 << 0)

typedef struct mx_display_info {
    unsigned format;
    unsigned width;
    unsigned height;
    unsigned stride;
    unsigned pixelsize;
    unsigned flags;
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


#define DISPLAY_OP_GET_FB 0x7FFF0001
typedef struct {
    mx_handle_t vmo;
    mx_display_info_t info;
} ioctl_display_get_fb_t;

#define DISPLAY_OP_FLUSH_FB 2
