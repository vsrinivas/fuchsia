// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/pixelformat.h>
#include <magenta/device/display.h>

__BEGIN_CDECLS;

/**
 * protocol/display.h - display protocol definitions
 */

typedef void (*mx_display_cb_t)(bool acquired, void* cookie);

typedef struct display_protocol_ops {
    // sets the display mode
    mx_status_t (*set_mode)(void* ctx, mx_display_info_t* info);

    // gets the display mode
    mx_status_t (*get_mode)(void* ctx, mx_display_info_t* info);

    // gets a pointer to the framebuffer
    mx_status_t (*get_framebuffer)(void* ctx, void** framebuffer);

    // flushes the framebuffer
    void (*flush)(void* ctx);

    // Controls ownership of the display between multiple display clients.
    // Useful for switching to and from the gfxconsole.
    // If the framebuffer is visible, release ownership of the display and
    // allow other clients to scanout buffers.
    // If the framebuffer is not visible, make it visible and acquire ownership
    // of the display, preventing other clients from scanning out buffers.
    // If the display is owned when when a new graphics client is created,
    // ownership will automatically be released.
    void (*acquire_or_release_display)(void* ctx, bool acquire);

    // Registers a callback to be invoked when display ownership changes.
    // The provided callback will be invoked with a value of true if the display
    // has been acquired, false if it has been released.
    void (*set_ownership_change_callback)(void* ctx, mx_display_cb_t callback, void* cookie);
} display_protocol_ops_t;

typedef struct mx_display_protocol {
    display_protocol_ops_t* ops;
    void* ctx;
} display_protocol_t;
__END_CDECLS;
