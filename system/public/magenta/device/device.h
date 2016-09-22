// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

// Bind to a driver
//   in: driver to bind to (optional)
//   out: none
#define IOCTL_DEVICE_BIND \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 0)

// Watch a directory for changes
//   in: none
//   out: handle to msgpipe to get notified on
#define IOCTL_DEVICE_WATCH_DIR \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVICE, 1)

// Return a handle to the device event
//   in: none
//   out: handle
#define IOCTL_DEVICE_GET_EVENT_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVICE, 2)

// Return driver name string
//   in: none
//   out: null-terminated string
#define IOCTL_DEVICE_GET_DRIVER_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 3)

// Return device name string
//   in: none
//   out: null-terminated string
#define IOCTL_DEVICE_GET_DEVICE_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 4)

// Indicates if there's data available to read,
// or room to write, or an error condition.
#define DEVICE_SIGNAL_READABLE MX_SIGNAL_SIGNAL0
#define DEVICE_SIGNAL_WRITABLE MX_SIGNAL_SIGNAL1
#define DEVICE_SIGNAL_ERROR MX_SIGNAL_SIGNAL2

// ssize_t ioctl_device_bind(int fd, const char* in, size_t in_len);
IOCTL_WRAPPER_VARIN(ioctl_device_bind, IOCTL_DEVICE_BIND, char);

// ssize_t ioctl_device_watch_dir(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_device_watch_dir, IOCTL_DEVICE_WATCH_DIR, mx_handle_t);

// ssize_t ioctl_device_get_event_handle(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_device_get_event_handle, IOCTL_DEVICE_GET_EVENT_HANDLE, mx_handle_t);

// ssize_t ioctl_device_get_driver_name(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_device_get_driver_name, IOCTL_DEVICE_GET_DRIVER_NAME, char);

// ssize_t ioctl_device_get_device_name(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_device_get_device_name, IOCTL_DEVICE_GET_DEVICE_NAME, char);
