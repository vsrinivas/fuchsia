// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <ddk/ioctl.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/pixelformat.h>
#include <magenta/device/display.h>

__BEGIN_CDECLS;

/**
 * protocol/display.h - display protocol definitions
 */

typedef struct mx_display_protocol {
    mx_status_t (*set_mode)(mx_device_t* dev, mx_display_info_t* info);
    // sets the display mode

    mx_status_t (*get_mode)(mx_device_t* dev, mx_display_info_t* info);
    // gets the display mode

    mx_status_t (*get_framebuffer)(mx_device_t* dev, void** framebuffer);
    // gets a pointer to the framebuffer

    void (*flush)(mx_device_t* dev);
    // flushes the framebuffer

    void (*acquire_or_release_display)(mx_device_t* dev);
    // Controls ownership of the display between multiple display clients.
    // Useful for switching to and from the gfxconsole.
    // If the framebuffer is visible, release ownership of the display and
    // allow other clients to scanout buffers.
    // If the framebuffer is not visible, make it visible and acquire ownership
    // of the display, preventing other clients from scanning out buffers.
    // If the display is owned when when a new graphics client is created,
    // ownership will automatically be released.

} mx_display_protocol_t;

__END_CDECLS;
