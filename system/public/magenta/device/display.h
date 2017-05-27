// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

#define MX_DISPLAY_FLAG_HW_FRAMEBUFFER (1 << 0)

typedef struct mx_display_info {
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixelsize;
    uint32_t flags;
} mx_display_info_t;

// Return the framebuffer
//   in: none
//   out: ioctl_display_get_fb_t
#define IOCTL_DISPLAY_GET_FB \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DISPLAY, 1)

// Flush the framebuffer
//   in: none
//   out: none
#define IOCTL_DISPLAY_FLUSH_FB \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DISPLAY, 2)

// Flush a region in the framebuffer
//   in: ioctl_display_region_t
//   out: none
#define IOCTL_DISPLAY_FLUSH_FB_REGION \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DISPLAY, 3)

// Set display fullscreen
//   in: uint32_t
//   out: none
#define IOCTL_DISPLAY_SET_FULLSCREEN \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DISPLAY, 4)

// Get an event to signal display ownership changes
// The event will be signaled with USER_SIGNAL_0 when
// the virtual console takes control of the display,
// and with USER_SIGNAL_1 when it is released for use
// by other clients.
//  in: none
//  out: mx_handle_t
#define IOCTL_DISPLAY_GET_OWNERSHIP_CHANGE_EVENT \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DISPLAY, 5)

// in: uint32_t owner
// out: none
#define IOCTL_DISPLAY_SET_OWNER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DISPLAY, 6)

typedef struct {
    mx_handle_t vmo;
    mx_display_info_t info;
} ioctl_display_get_fb_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} ioctl_display_region_t;

// ssize_t ioctl_display_get_fb(int fd, ioctl_display_get_fb_t* out);
IOCTL_WRAPPER_OUT(ioctl_display_get_fb, IOCTL_DISPLAY_GET_FB, ioctl_display_get_fb_t);

// ssize_t ioctl_display_flush_fb(int fd);
IOCTL_WRAPPER(ioctl_display_flush_fb, IOCTL_DISPLAY_FLUSH_FB);

// ssize_t ioctl_display_flush_fb_region(int fd, const ioctl_display_region_t* in);
IOCTL_WRAPPER_IN(ioctl_display_flush_fb_region, IOCTL_DISPLAY_FLUSH_FB_REGION, ioctl_display_region_t);

// ssize_t ioctl_display_set_fullscreen(int fd, uint32_t in);
IOCTL_WRAPPER_IN(ioctl_display_set_fullscreen, IOCTL_DISPLAY_SET_FULLSCREEN, uint32_t);

// ssize_t ioctl_display_get_ownership_change_event(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_display_get_ownership_change_event, IOCTL_DISPLAY_GET_OWNERSHIP_CHANGE_EVENT, mx_handle_t);

// ssize_t ioctl_display_set_owner(int fd, uint32_t owner);
IOCTL_WRAPPER_IN(ioctl_display_set_owner, IOCTL_DISPLAY_SET_OWNER, uint32_t)