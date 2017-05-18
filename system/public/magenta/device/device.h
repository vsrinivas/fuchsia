// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

// Bind to a driver
//   in: path to the driver to bind (optional)
//   out: none
#define IOCTL_DEVICE_BIND \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 0)

// Return a handle to the device event
//   in: none
//   out: handle
#define IOCTL_DEVICE_GET_EVENT_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVICE, 1)

// Return driver name string
//   in: none
//   out: null-terminated string
#define IOCTL_DEVICE_GET_DRIVER_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 2)

// Return device name string
//   in: none
//   out: null-terminated string
#define IOCTL_DEVICE_GET_DEVICE_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 3)

// Suspends the device
// (intended for driver suspend/resume testing)
//   in: none
//   out: none
#define IOCTL_DEVICE_DEBUG_SUSPEND \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 4)

// Resumes the device
// (intended for driver suspend/resume testing)
//   in: none
//   out: none
#define IOCTL_DEVICE_DEBUG_RESUME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 5)

// Sync the device
//   in: none
//   out: none
#define IOCTL_DEVICE_SYNC \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 6)

// Indicates if there's data available to read,
// or room to write, or an error condition.
#define DEVICE_SIGNAL_READABLE MX_USER_SIGNAL_0
#define DEVICE_SIGNAL_OOB      MX_USER_SIGNAL_1
#define DEVICE_SIGNAL_WRITABLE MX_USER_SIGNAL_2
#define DEVICE_SIGNAL_ERROR    MX_USER_SIGNAL_3
#define DEVICE_SIGNAL_HANGUP   MX_USER_SIGNAL_4

// ssize_t ioctl_device_bind(int fd, const char* in, size_t in_len);
IOCTL_WRAPPER_VARIN(ioctl_device_bind, IOCTL_DEVICE_BIND, char);

// ssize_t ioctl_device_get_event_handle(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_device_get_event_handle, IOCTL_DEVICE_GET_EVENT_HANDLE, mx_handle_t);

// ssize_t ioctl_device_get_driver_name(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_device_get_driver_name, IOCTL_DEVICE_GET_DRIVER_NAME, char);

// ssize_t ioctl_device_get_device_name(int fd, char* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_device_get_device_name, IOCTL_DEVICE_GET_DEVICE_NAME, char);

// ssize_t ioctl_device_debug_suspend(int fd);
IOCTL_WRAPPER(ioctl_device_debug_suspend, IOCTL_DEVICE_DEBUG_SUSPEND);

// ssize_t ioctl_device_debug_resume(int fd);
IOCTL_WRAPPER(ioctl_device_debug_resume, IOCTL_DEVICE_DEBUG_RESUME);

// ssize_t ioctl_device_sync(int fd);
IOCTL_WRAPPER(ioctl_device_sync, IOCTL_DEVICE_SYNC);
