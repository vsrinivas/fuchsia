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


#define CONSOLE_OP_GET_DIMENSIONS 1

typedef struct {
    uint32_t width;
    uint32_t height;
} ioctl_console_dimensions_t;