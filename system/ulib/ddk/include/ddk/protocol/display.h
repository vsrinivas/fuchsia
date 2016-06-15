// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
    uint format;
    int width;
    int height;
    int stride;
    uint flags;
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
